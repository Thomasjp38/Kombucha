#pragma once

#include <Arduino.h>
#include <Adafruit_TCS34725.h>

struct TCSRawReading {
  uint16_t clear = 0;
  uint16_t red = 0;
  uint16_t green = 0;
  uint16_t blue = 0;
  bool ok = false;
};

struct TCSHSLReading {
  // Hue in degrees [0, 360), Saturation/Lightness in [0, 1]
  float hue = 0.0f;
  float saturation = 0.0f;
  float lightness = 0.0f;
  bool ok = false;
};

class ColorSensorTCS34725 {
public:
  ColorSensorTCS34725(
      tcs34725IntegrationTime_t integrationTime = TCS34725_INTEGRATIONTIME_50MS,
      tcs34725Gain_t gain = TCS34725_GAIN_4X);

  bool begin();

  // Reads clear/red/green/blue channels directly from sensor.
  TCSRawReading readRaw();

  // Reads sensor and converts normalized RGB to HSL.
  TCSHSLReading readHSL();

private:
  static TCSHSLReading rgbToHsl(float r, float g, float b);

  Adafruit_TCS34725 _tcs;
};
