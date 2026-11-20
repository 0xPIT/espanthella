#pragma once
#include <cstdint>

struct LightState {
  bool on;
  uint8_t brightness;      // 1..255, retained while off; setter accepts 0 as off
  uint16_t temperature;    // mireds; fixed default for single-channel hardware
};

bool initLight();           // Creates the endpoint before Matter.begin().
void restoreLightOutput();  // Applies saved output before WiFi provisioning can block.
bool refreshLightState();   // Align cache and report saved state after MATTER_SERVER_READY; retry on failure.
bool reportLightState();     // Re-report current state when a Matter session connects.
void handleLight();         // Services PWM and persists changed state from loop().
LightState getLightState();
bool supportsColorTemperature();
bool isCwWwSwapped();
bool setCwWwSwapped(bool swapped);  // false on single-channel hardware or failed save
bool setLightOnOff(bool on);
bool setLightBrightness(uint8_t brightness);
bool setLightTemperature(uint16_t mireds);  // false for single-channel hardware
bool toggleLight();
void flushLightPrefs();     // Save pending state before an intentional reboot.
void clearLightPrefs();
void handleTouch();
