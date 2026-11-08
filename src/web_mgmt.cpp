//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#include "web_mgmt.h"
#include "config.h"
#include "identity.h"
#include "light.h"
#include "wifi_portal.h"
#include "fs_html.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Matter.h>
#include <LittleFS.h>

static WebServer server(80);

// ── Helpers ──────────────────────────────────────────────────────────

static String formatUptime() {
  uint32_t s = millis() / 1000;
  uint32_t d = s / 86400; s %= 86400;
  uint32_t h = s / 3600;  s %= 3600;
  uint32_t m = s / 60;    s %= 60;
  String out;
  if (d > 0) out += String(d) + "d ";
  if (h > 0 || d > 0) out += String(h) + "h ";
  out += String(m) + "m " + String(s) + "s";
  return out;
}

static String actionPage(const char *title, const char *heading, const char *message) {
  String page = readHtmlFile("/action.html");
  page.replace("%TITLE%",   title);
  page.replace("%HEADING%", heading);
  page.replace("%MESSAGE%", message);
  return page;
}

// ── Dashboard ────────────────────────────────────────────────────────

static void handleRoot() {
  bool commissioned = Matter.isDeviceCommissioned();
  uint8_t brightness = DimmableLight.getBrightness();

  String qrPayload;
  if (!commissioned) {
    String qrUrl = Matter.getOnboardingQRCodeUrl();
    int32_t idx = qrUrl.indexOf("data=");
    if (idx >= 0) {
      qrPayload = qrUrl.substring(idx + 5);
    }
  }

  String page = readHtmlFile("/status.html");
  page.replace("%WIFI_SSID%",     WiFi.SSID());
  page.replace("%WIFI_IP%",       WiFi.localIP().toString());
  page.replace("%WIFI_RSSI%",     String(WiFi.RSSI()));
  page.replace("%WIFI_MAC%",      WiFi.macAddress());
  page.replace("%MATTER_STATUS%", commissioned ? "Commissioned" : "Not commissioned");
  page.replace("%LIGHT_STATE%",   DimmableLight ? "ON" : "OFF");
  page.replace("%LIGHT_CHECKED%", DimmableLight ? "checked" : "");
  page.replace("%BRIGHTNESS%",    String(brightness * 100 / 255) + "%");
  page.replace("%BRIGHTNESS_RAW%", String(brightness));
  page.replace("%PAIR_DISPLAY%",  commissioned ? "display:none" : "");
  page.replace("%PAIRING_CODE%",  commissioned ? "" : Matter.getManualPairingCode());
  page.replace("%QR_PAYLOAD%",    qrPayload);
  page.replace("%HOSTNAME%",      String(hostName) + ".local");
  page.replace("%UPTIME%",        formatUptime());
  page.replace("%FREE_HEAP%",     String(ESP.getFreeHeap() / 1024));
  page.replace("%CHIP_MODEL%",    String(ESP.getChipModel()));
  page.replace("%FLASH_SIZE%",    String(ESP.getFlashChipSize() / (1024 * 1024)));
  server.send(200, "text/html", page);
}

// ── Reset / reboot handlers ──────────────────────────────────────────

static void handleResetMatter() {
  Serial.println("Web: Matter decommission requested");
  DimmableLight = false;
  Matter.decommission();
  server.send(200, "text/html",
    actionPage("Matter Reset",
               "&#9888;&#65039; Matter Decommissioned",
               "The device has been removed from its fabric.<br>"
               "You can now pair it to a new controller."));
}

static void handleResetWifi() {
  Serial.println("Web: WiFi credential reset requested");
  clearWiFiCredentials();
  server.send(200, "text/html",
    actionPage("WiFi Reset",
               "&#128246; WiFi Credentials Erased",
               "Rebooting into setup AP mode&hellip;"));
  delay(1500);
  ESP.restart();
}

static void handleFactoryReset() {
  Serial.println("Web: Factory reset requested");
  DimmableLight = false;
  Matter.decommission();
  clearWiFiCredentials();
  clearLightPrefs();
  server.send(200, "text/html",
    actionPage("Factory Reset",
               "&#128165; Factory Reset Complete",
               "All provisioning data erased.<br>Rebooting into setup mode&hellip;"));
  delay(1500);
  ESP.restart();
}

static void handleReboot() {
  Serial.println("Web: Reboot requested");
  server.send(200, "text/html",
    actionPage("Reboot",
               "&#128260; Rebooting&hellip;",
               "The device will be back online in a few seconds."));
  delay(1000);
  ESP.restart();
}

// ── OTA update (combined FW+FS image, or plain firmware) ────────────
//
// Combined format (little-endian):
//   [0..3]  magic   "ESPB" (0x42505345)
//   [4..7]  fw_size uint32_t
//   [8..11] fs_size uint32_t
//   [12..]  firmware bytes, then filesystem bytes
//
// If the first four bytes are not the magic, the upload is treated as a
// plain firmware image (backward-compatible with raw .bin files).

static constexpr uint32_t OTA_MAGIC  = 0x42505345;  // "ESPB" LE
static constexpr size_t   OTA_HDR_SZ = 12;

enum OtaPhase : uint8_t { OTA_HEADER, OTA_FW, OTA_FS, OTA_ERROR };
static OtaPhase otaPhase;
static bool     otaHasFs;
static uint32_t otaFwSize, otaFsSize, otaFwDone;
static uint8_t  otaHdr[OTA_HDR_SZ];
static size_t   otaHdrPos;

static void otaWrite(uint8_t *data, size_t len) {
  while (len && otaPhase != OTA_ERROR) {
    if (otaPhase == OTA_FW) {
      size_t n = otaHasFs ? min(len, (size_t)(otaFwSize - otaFwDone)) : len;
      if (Update.write(data, n) != n) {
        Update.printError(Serial); otaPhase = OTA_ERROR; return;
      }
      data += n; len -= n;
      if (otaHasFs) {
        otaFwDone += n;
        if (otaFwDone >= otaFwSize) {
          if (!Update.end(true)) {
            Update.printError(Serial); otaPhase = OTA_ERROR; return;
          }
          Serial.printf("OTA: firmware OK (%lu B)\n", (unsigned long)otaFwSize);
          if (!Update.begin(otaFsSize, U_SPIFFS)) {
            Update.printError(Serial); otaPhase = OTA_ERROR; return;
          }
          otaPhase = OTA_FS;
        }
      }
    }
    else if (otaPhase == OTA_FS) {
      if (Update.write(data, len) != len) {
        Update.printError(Serial); otaPhase = OTA_ERROR; return;
      }
      len = 0;
    }
    else break;
  }
}

static void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("OTA: receiving %s\n", up.filename.c_str());
    otaPhase  = OTA_HEADER;
    otaHasFs  = false;
    otaFwSize = otaFsSize = otaFwDone = 0;
    otaHdrPos = 0;
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaPhase == OTA_ERROR) return;
    uint8_t *data = up.buf;
    size_t   len  = up.currentSize;

    if (otaPhase == OTA_HEADER) {
      while (otaHdrPos < OTA_HDR_SZ && len) {
        otaHdr[otaHdrPos++] = *data++; len--;
      }
      if (otaHdrPos < OTA_HDR_SZ) return;       // need more bytes

      uint32_t magic;
      memcpy(&magic,     otaHdr,     4);
      memcpy(&otaFwSize, otaHdr + 4, 4);
      memcpy(&otaFsSize, otaHdr + 8, 4);

      if (magic == OTA_MAGIC && otaFwSize > 0) { // combined image
        otaHasFs = otaFsSize > 0;
        Serial.printf("OTA: combined — fw=%lu  fs=%lu\n", (unsigned long)otaFwSize, (unsigned long)otaFsSize);
        if (!Update.begin(otaFwSize, U_FLASH)) {
          Update.printError(Serial); otaPhase = OTA_ERROR; return;
        }
      }
      else {                                      // plain firmware
        otaHasFs = false;
        Serial.println("OTA: plain firmware");
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          Update.printError(Serial); otaPhase = OTA_ERROR; return;
        }
        if (Update.write(otaHdr, OTA_HDR_SZ) != OTA_HDR_SZ) {
          Update.printError(Serial); otaPhase = OTA_ERROR; return;
        }
      }
      otaPhase = OTA_FW;
    }

    if (len) otaWrite(data, len);
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (otaPhase == OTA_HEADER) {
      Serial.println("OTA: image too small");
      otaPhase = OTA_ERROR; return;
    }
    if (otaPhase == OTA_ERROR) return;
    if (!Update.end(true)) {
      Update.printError(Serial); otaPhase = OTA_ERROR; return;
    }
    Serial.printf("OTA: %s OK — %lu B total\n",
                  otaPhase == OTA_FS ? "filesystem" : "firmware",
                  (unsigned long)up.totalSize);
  }
}

static void handleUpdateResult() {
  if (otaPhase == OTA_ERROR) {
    server.send(500, "text/plain", "Update failed");
  }
  else {
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
  }
}

// ── Light control API ────────────────────────────────────────────────

static void handleGetLight() {
  char json[64];
  snprintf(json, sizeof(json), "{\"on\":%s,\"brightness\":%u}",
           (bool)DimmableLight ? "true" : "false",
           DimmableLight.getBrightness());
  server.send(200, "application/json", json);
}

static void handleSetLight() {
  bool    on  = (bool)DimmableLight;
  uint8_t bri = DimmableLight.getBrightness();

  if (server.hasArg("on"))         on  = server.arg("on") != "0";
  if (server.hasArg("brightness")) bri = constrain(server.arg("brightness").toInt(), 0, 255);

  DimmableLight.setOnOff(on);
  if (on) DimmableLight.setBrightness(bri);

  handleGetLight();
}

// ── Public API ───────────────────────────────────────────────────────

void startManagementServer() {
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/qrcode.min.js", LittleFS, "/qrcode.min.js");
  server.serveStatic("/favicon.svg", LittleFS, "/favicon.svg");
  server.on("/",              HTTP_GET,  handleRoot);
  server.on("/api/light",     HTTP_GET,  handleGetLight);
  server.on("/api/light",     HTTP_POST, handleSetLight);
  server.on("/reset-matter",  HTTP_GET,  handleResetMatter);
  server.on("/reset-wifi",    HTTP_GET,  handleResetWifi);
  server.on("/factory-reset", HTTP_GET,  handleFactoryReset);
  server.on("/reboot",        HTTP_GET,  handleReboot);
  server.on("/update",        HTTP_GET,  []() {
   server.send(200, "text/html", readHtmlFile("/ota.html"));
  });
  server.on("/update",        HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.begin();

  if (MDNS.begin(hostName)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS started — http://%s.local/\n", hostName);
  }
  else {
    Serial.println("mDNS failed to start");
  }

  Serial.printf("Management server started — http://%s/\n", WiFi.localIP().toString().c_str());
}

void handleWebClients() {
  server.handleClient();
}
