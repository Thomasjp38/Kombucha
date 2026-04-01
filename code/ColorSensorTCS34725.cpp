#include "ColorSensorTCS34725.h"
#include <math.h>

ColorSensorTCS34725::ColorSensorTCS34725(
    tcs34725IntegrationTime_t integrationTime,
    tcs34725Gain_t gain)
    : _tcs(integrationTime, gain) {}

bool ColorSensorTCS34725::begin() {
  return _tcs.begin();
}

TCSRawReading ColorSensorTCS34725::readRaw() {
  TCSRawReading reading;

  _tcs.getRawData(&reading.red, &reading.green, &reading.blue, &reading.clear);

  // Treat zero clear channel as invalid/no-light read.
  reading.ok = (reading.clear > 0);
  return reading;
}

TCSHSLReading ColorSensorTCS34725::readHSL() {
  TCSRawReading raw = readRaw();
  if (!raw.ok) {
    return TCSHSLReading{};
  }

  // Normalize by clear channel to reduce light intensity dependency.
  float r = min(1.0f, static_cast<float>(raw.red) / static_cast<float>(raw.clear));
  float g = min(1.0f, static_cast<float>(raw.green) / static_cast<float>(raw.clear));
  float b = min(1.0f, static_cast<float>(raw.blue) / static_cast<float>(raw.clear));

  TCSHSLReading hsl = rgbToHsl(r, g, b);
  hsl.ok = true;
  return hsl;
}

TCSHSLReading ColorSensorTCS34725::rgbToHsl(float r, float g, float b) {
  TCSHSLReading out;

  float maxVal = max(r, max(g, b));
  float minVal = min(r, min(g, b));
  float delta = maxVal - minVal;

  out.lightness = (maxVal + minVal) * 0.5f;

  if (delta <= 0.00001f) {
    out.hue = 0.0f;
    out.saturation = 0.0f;
    return out;
  }

  out.saturation =
      delta / (1.0f - fabsf(2.0f * out.lightness - 1.0f));

  if (maxVal == r) {
    out.hue = 60.0f * fmodf(((g - b) / delta), 6.0f);
  } else if (maxVal == g) {
    out.hue = 60.0f * (((b - r) / delta) + 2.0f);
  } else {
    out.hue = 60.0f * (((r - g) / delta) + 4.0f);
  }

  if (out.hue < 0.0f) {
    out.hue += 360.0f;
  }

  return out;
}
