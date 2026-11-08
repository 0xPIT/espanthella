#pragma once
#include <cstdint>
#include <Matter.h>

extern MatterDimmableLight DimmableLight;

void  initLight();
bool  setLightState(bool state, uint8_t brightness);
void  clearLightPrefs();

void  handleTouch();
