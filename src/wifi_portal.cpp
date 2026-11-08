//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#include "wifi_portal.h"
#include "config.h"
#include "identity.h"
#include "fs_html.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>

static Preferences wifiPref;
static WebServer   portalServer(80);
static DNSServer   dnsServer;
static bool        credsSaved = false;
static String      cachedNetworks;

// ── Network scan ─────────────────────────────────────────────────────

static String buildNetworkOptions() {
  Serial.println("Scanning WiFi networks…");
  int16_t n = WiFi.scanNetworks();
  String opts;
  if (n <= 0) {
    opts = "<option value=\"\">No networks found</option>";
  }
  else {
    for (int16_t i = 0; i < n; i++) {
      bool dup = false;
      for (int16_t j = 0; j < i; j++) {
        if (WiFi.SSID(j) == WiFi.SSID(i)) { 
          dup = true; break;
        }
      }
      if (dup || WiFi.SSID(i).isEmpty()) {
        continue;
      }
      opts += "<option value=\"" + WiFi.SSID(i) + "\">"
            + WiFi.SSID(i)
            + " (" + String(WiFi.RSSI(i)) + " dBm"
            + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "" : ", 🔒")
            + ")</option>";
    }
  }
  WiFi.scanDelete();
  return opts;
}

// ── Portal HTTP handlers ─────────────────────────────────────────────

static void handleSave() {
  String ssid = portalServer.arg("manual_ssid");
  if (ssid.isEmpty()) {
    ssid = portalServer.arg("ssid");
  }
  String password = portalServer.arg("password");

  if (ssid.isEmpty()) {
    portalServer.sendHeader("Location", "/", true);
    portalServer.send(302, "text/plain", "SSID required");
    return;
  }

  wifiPref.begin(WIFI_PREF_NAMESPACE, false);
  wifiPref.putString("ssid", ssid);
  wifiPref.putString("password", password);
  wifiPref.end();
  Serial.printf("WiFi credentials saved — SSID: %s\n", ssid.c_str());

  String page = readHtmlFile("/portal_saved.html");
  page.replace("%SSID%", ssid);
  portalServer.send(200, "text/html", page);
  credsSaved = true;
}

// ── WiFi connection ──────────────────────────────────────────────────

static bool wifiConnect(const String &ssid, const String &password) {
  Serial.printf("Connecting to WiFi \"%s\"…", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected — IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("Connection failed.");
  WiFi.disconnect();
  return false;
}

// ── Captive portal (blocks until credentials saved → reboots) ────────

static void runCaptivePortal() {
  Serial.println("Starting captive portal AP…");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, WIFI_AP_PASSWORD);
  Serial.printf("AP SSID: %s  IP: %s\n",
                apSsid, WiFi.softAPIP().toString().c_str());

  cachedNetworks = buildNetworkOptions();
  dnsServer.start(53, "*", WiFi.softAPIP());

  portalServer.serveStatic("/style.css", LittleFS, "/style.css");
  portalServer.serveStatic("/favicon.svg", LittleFS, "/favicon.svg");
  portalServer.onNotFound([]() {
    portalServer.sendHeader("Location", "http://192.168.4.1/", true);
    portalServer.send(302, "text/plain", "");
  });

  portalServer.on("/", HTTP_GET, []() {
    String page = readHtmlFile("/portal.html");
    page.replace("%NETWORKS%", cachedNetworks);
    portalServer.send(200, "text/html", page);
  });

  portalServer.on("/scan", HTTP_GET, []() {
    cachedNetworks = buildNetworkOptions();
    portalServer.sendHeader("Location", "/", true);
    portalServer.send(302, "text/plain", "");
  });

  portalServer.on("/save", HTTP_POST, handleSave);

  portalServer.begin();

  credsSaved = false;
  while (!credsSaved) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
    delay(2);
  }

  uint32_t t = millis();
  while (millis() - t < 2000) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
    delay(2);
  }

  portalServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  Serial.println("Rebooting with new WiFi credentials…");
  delay(500);
  ESP.restart();
}

// ── Public API ───────────────────────────────────────────────────────

void clearWiFiCredentials() {
  wifiPref.begin(WIFI_PREF_NAMESPACE, false);
  wifiPref.clear();
  wifiPref.end();
}

void provisionWiFi() {
  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println("BOOT button held — waiting 3 s to confirm WiFi reset…");
    delay(3000);
    if (digitalRead(PIN_BUTTON) == LOW) {
      Serial.println("Clearing stored WiFi credentials!");
      clearWiFiCredentials();
    }
  }

  wifiPref.begin(WIFI_PREF_NAMESPACE, true);
  String ssid     = wifiPref.getString("ssid", "");
  String password  = wifiPref.getString("password", "");
  wifiPref.end();

  if (ssid.length() > 0) {
    if (wifiConnect(ssid, password)) {
      return;
    }
    Serial.println("Stored credentials failed — entering captive portal.");
  } 
  else {
    Serial.println("No stored WiFi credentials — entering captive portal.");
  }

  runCaptivePortal();
}
