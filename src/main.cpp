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
#include "web_mgmt.h"

static MatterButton button;
static bool wasCommissioned = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  buildDeviceIdentity();
  Serial.printf("\nespANTHELLA [%s] starting\n", deviceId);

  initFileSystem();
  initLight();
  button.begin(PIN_BUTTON, INPUT_PULLUP);

  provisionWiFi();

  Matter.begin();

  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Matter not commissioned — scan QR code or enter pairing code.");
    Serial.printf("  Manual code : %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("  QR code URL : %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }

  startManagementServer();

  Serial.printf("Initial state: %s | brightness: %u\r\n",
                DimmableLight ? "On" : "Off",
                DimmableLight.getBrightness());

  DimmableLight.updateAccessory();
}

void loop() {
  handleWebClients();
  handleTouch();

  if (Matter.isDeviceCommissioned()) {
    wasCommissioned = true;
  } 
  else if (wasCommissioned) {
    Serial.println("Matter fabric removed. Restarting.");
    delay(500);
    ESP.restart();
  }

  matterButtonEvent_t ev;
  while ((ev = button.poll()) != MATTER_BUTTON_NONE) {
    if (ev == MATTER_BUTTON_CLICK) {
      Serial.println("Button: toggling light");
      DimmableLight.toggle();
    } 
    else if (ev == MATTER_BUTTON_LONG_HOLD) {
      Serial.println("Button: decommissioning");
      DimmableLight = false;
      Matter.decommission();
    }
  }
}
