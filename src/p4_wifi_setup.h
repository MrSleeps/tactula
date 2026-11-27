#pragma once
#include <Arduino.h>
#include <lvgl.h>

// ESP32-P4+ESP32-C6 hosted-radio onboarding for Tactula.
// Uses the same AP naming and 'touchdeck' NVS ssid/pass keys as the S3.
void p4WifiBegin();
void p4WifiCreateUi(lv_obj_t* parent);
void p4WifiTick();
bool p4WifiConnected();
