//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#include <Arduino.h>
#include <Matter.h>

#include "config.h"
#include "identity.h"
#include "fs_html.h"
#include "wifi_portal.h"
#include "light.h"
#include "matter_pairing.h"
#include "antenna.h"
#include "web_mgmt.h"

static MatterButton button;
static bool wasCommissioned = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  buildDeviceIdentity();
  Serial.printf("\nespANTHELLA [%s] starting\n", deviceId);

  initFileSystem();
  if (!initLight()) {
    Serial.println("Light initialization failed; stopping before Matter starts.");
    while (true) delay(1000);
  }
  button.begin(PIN_BUTTON, INPUT_PULLUP);

#if defined(ARDUINO_XIAO_ESP32C6)
  initAntenna();  // Restore RF path before starting the station or setup AP.
#endif
  provisionWiFi();

  Matter.begin();
  refreshLightState();

  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Matter not commissioned — scan QR code or enter pairing code.");
    const auto pairingCode = MatterPairing::formatManualCode(Matter.getManualPairingCode().c_str());
    Serial.printf("  Manual code : %s\n", pairingCode.empty() ? "Unavailable" : pairingCode.c_str());
    Serial.printf("  QR code URL : %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }

  startManagementServer();

  const LightState state = getLightState();
  Serial.printf("Initial state: %s | brightness: %u | temperature control: %s\r\n",
                state.on ? "On" : "Off", state.brightness,
                supportsColorTemperature() ? "WW/CW" : "fixed");
}

void loop() {
  handleWebClients();
#if defined(ARDUINO_XIAO_ESP32C6)
  handleAntenna();
#endif
  handleTouch();
  handleLight();

  if (Matter.isDeviceCommissioned()) {
    wasCommissioned = true;
  } 
  else if (wasCommissioned) {
    Serial.println("Matter fabric removed. Restarting.");
    flushLightPrefs();
    delay(500);
    ESP.restart();
  }

  matterButtonEvent_t ev;
  while ((ev = button.poll()) != MATTER_BUTTON_NONE) {
    if (ev == MATTER_BUTTON_CLICK) {
      Serial.println("Button: toggling light");
      toggleLight();
    } 
    else if (ev == MATTER_BUTTON_LONG_HOLD) {
      Serial.println("Button: decommissioning");
      setLightOnOff(false);
      handleLight();
      flushLightPrefs();
      Matter.decommission();
    }
  }
}
