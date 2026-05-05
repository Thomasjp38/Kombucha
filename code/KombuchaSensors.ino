// #include <Arduino.h>
#include <cmath>
#include <cstring>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_wifi.h>
#include <Wire.h>

#if __has_include(<esp_eap_client.h>)
#include <esp_eap_client.h>
#define HAS_ESP_EAP_CLIENT 1
#elif __has_include(<esp_wpa2.h>)
#include <esp_wpa2.h>
#define HAS_ESP_WPA2 1
#endif

#include "UltrasonicArray.h"
#include "TempSensorDS18B20.h"
#include "ColorSensorTCS34725.h"
#include "PhSensorAnalog.h"

// ============================================================
// DEBUG MODE
// true  = no sensors required, fake readings are generated
// false = real hardware mode
// ============================================================
const bool DEBUG_MODE = false;

// ---------------- Wi-Fi / MQTT config ----------------
const char* WIFI_SSID = "YourWIFI";
const bool WIFI_USE_ENTERPRISE = true;

// Replace these with your real values
const char* WIFI_IDENTITY = "";
const char* WIFI_USERNAME = "";
const char* WIFI_USER_PASSWORD = "";
const char* WIFI_PASSWORD = "";

const char* MQTT_HOST = "";
const uint16_t MQTT_PORT = 8883;
const char* MQTT_USERNAME = "";
const char* MQTT_PASSWORD = "";
const char* DEVICE_ID = "kombucha-esp32-01";

// ---------------- Pin map ----------------
constexpr int PIN_TEMP_DS18B20 = 25;
constexpr int PIN_PH_ANALOG = 35;   // change if your pH board output uses a different ADC pin
constexpr int PIN_PUMP_FEED = 12;
constexpr int PIN_PUMP_WASTE = 13;
constexpr int PIN_OXYGEN = 33;
constexpr int PIN_US_TRIG = 16;
constexpr int US_ECHO_PINS[] = {19, 18, 17}; // main, waste, feed
constexpr int PIN_HEAT_RELAY = 26;
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;

constexpr int IDX_MAIN = 0;
constexpr int IDX_WASTE = 1;
constexpr int IDX_FEED = 2;
constexpr int US_COUNT = 3;

constexpr bool HEAT_RELAY_ACTIVE_LOW = true;

// ---------------- Timing ----------------
constexpr unsigned long SENSOR_PERIOD_MS = 1500UL;
constexpr unsigned long TELEMETRY_PERIOD_MS = 3000UL;
constexpr unsigned long STATUS_PERIOD_MS = 5000UL;
constexpr unsigned long MQTT_RETRY_MS = 3000UL;

// ---------------- Runtime thresholds/parameters ----------------
struct ControlParams {
  float mainDistanceTargetCm = 10.0f;
  float mainDistanceBandCm = 1.0f;
  float mainTooFullDistanceCm = 4.0f;     // emergency high-level limit; smaller distance means fuller jar
  float feedEmptyDistanceCm = 15.0f;
  float wasteOverflowDistanceCm = 3.0f;
  float tempLowC = 23.0f;
  float tempHighC = 27.0f;
};

ControlParams params;

struct AlertFlags {
  // Live sensor conditions
  bool feedEmpty = false;
  bool wasteOverflow = false;
  bool mainJarTooFull = false;
  bool tempSensorFault = false;
  bool phSensorFault = false;
  bool colorSensorFault = false;  // live-only warning; not latched

  // Latched lockouts requiring user acknowledgement
  bool feedEmptyLatched = false;
  bool wasteOverflowLatched = false;
  bool mainJarTooFullLatched = false;
  bool tempSensorFaultLatched = false;

  // System-level lockouts
  bool fluidSystemLockout = false;  // blocks both fluid pumps
  bool heaterLockout = false;       // blocks heater
};

AlertFlags alerts;

// ---------------- Sensor objects ----------------
UltrasonicArray usArray(PIN_US_TRIG, US_ECHO_PINS, US_COUNT);
USReading usReadings[US_COUNT];
TempSensorDS18B20 tempSensor(PIN_TEMP_DS18B20);
ColorSensorTCS34725 colorSensor;
PhSensorAnalog phSensor(PIN_PH_ANALOG);

TCSRawReading colorRaw;
TCSHSLReading colorHsl;

// ---------------- Fixed RGB calibration values ----------------
// Replace these with values from your separate RGB calibration test.
constexpr float COLOR_BLACK_R = 169.0f;
constexpr float COLOR_BLACK_G = 114.2f;
constexpr float COLOR_BLACK_B = 81.4f;

constexpr float COLOR_WHITE_R = 2184.0f;
constexpr float COLOR_WHITE_G = 1525.2f;
constexpr float COLOR_WHITE_B = 995.2f;

// Corrected 0-255 RGB values for telemetry/dashboard
int colorR8 = 0;
int colorG8 = 0;
int colorB8 = 0;
bool colorCorrectedOk = false;

bool colorSensorConnected = false;
unsigned long lastColorRecoveryMs = 0;
constexpr unsigned long COLOR_RECOVERY_PERIOD_MS = 3000UL;

// ---------------- Actuator states ----------------
bool autoLevelControl = true;
bool autoHeaterControl = true;

bool feedPumpOn = false;
bool wastePumpOn = false;
bool oxygenPumpOn = false;
bool heaterOn = false;

// ---------------- Oxygen timer control ----------------
bool autoOxygenControl = false;

float oxygenRunSeconds = 60.0f;      // how long oxygen pump stays ON
float oxygenWaitSeconds = 60.0f;     // how long oxygen pump stays OFF between runs

bool oxygenCycleRunning = false;
unsigned long oxygenCycleStateStartMs = 0;

// ---------------- Cached sensor values ----------------
float tempC = NAN;
float phValue = NAN;
float phVoltage = NAN;

// ---------------- MQTT topics ----------------
String topicTelemetry;
String topicStatus;
String topicAlerts;
String topicCommandSet;
String topicCommandAck;
String topicConfigSet;
String topicConfigAck;

WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

// ============================================================
// Utility helpers
// ============================================================
float correctColorChannel(float raw, float black, float white);
void rgbToHslCorrected(float r, float g, float b, TCSHSLReading& out);
void updateCorrectedColorFromRaw();

void recomputeSystemLockouts();
bool isWasteOverflowNow();
bool isFeedEmptyNow();
bool isMainJarTooFullNow();
bool isTempSensorFaultNow();
bool isColorSensorFaultNow();
void clearFluidLockoutIfSafe();
void clearFeedEmptyAlertIfSafe();
void clearWasteOverflowAlertIfSafe();
void clearMainJarTooFullAlertIfSafe();
void clearTempSensorFaultIfSafe();

void readColorSensorWithRecovery() {
  colorRaw = colorSensor.readRaw();

  if (colorRaw.ok && colorRaw.clear > 0) {
    colorSensorConnected = true;
    updateCorrectedColorFromRaw();
    return;
  }

  colorSensorConnected = false;
  colorHsl = {};
  colorCorrectedOk = false;

  const unsigned long now = millis();

  if (now - lastColorRecoveryMs >= COLOR_RECOVERY_PERIOD_MS) {
    lastColorRecoveryMs = now;

    Serial.println("RGB sensor invalid. Trying to reinitialize TCS34725...");

    // Reinitialize I2C and sensor after SDA/SCL disruption.
    Wire.end();
    delay(50);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    delay(50);

    colorSensorConnected = colorSensor.begin();

    if (colorSensorConnected) {
      Serial.println("TCS34725 recovery successful.");
      colorRaw = colorSensor.readRaw();
      updateCorrectedColorFromRaw();
    } else {
      Serial.println("TCS34725 recovery failed.");
    }
  }
}

bool isFinitePositive(float v) {
  return std::isfinite(v) && v > 0.0f;
}

float extractFloat(const String& s, const char* key, float fallback) {
  const String k = String("\"") + key + "\":";
  int idx = s.indexOf(k);
  if (idx < 0) return fallback;
  int valueStart = idx + k.length();
  while (valueStart < (int)s.length() &&
         (s[valueStart] == ' ' || s[valueStart] == '\t' || s[valueStart] == '"')) {
    valueStart++;
  }

  int valueEnd = valueStart;
  while (valueEnd < (int)s.length()) {
    const char c = s[valueEnd];
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') valueEnd++;
    else break;
  }

  if (valueEnd <= valueStart) return fallback;
  return s.substring(valueStart, valueEnd).toFloat();
}

bool extractBool(const String& s, const char* key, bool fallback) {
  const String k = String("\"") + key + "\":";
  int idx = s.indexOf(k);
  if (idx < 0) return fallback;
  int valueStart = idx + k.length();
  while (valueStart < (int)s.length() && (s[valueStart] == ' ' || s[valueStart] == '\t')) valueStart++;
  if (s.startsWith("true", valueStart) || s.startsWith("1", valueStart)) return true;
  if (s.startsWith("false", valueStart) || s.startsWith("0", valueStart)) return false;
  return fallback;
}

float correctColorChannel(float raw, float black, float white) {
  const float denominator = white - black;

  if (denominator <= 1.0f) {
    return 0.0f;
  }

  float corrected = (raw - black) / denominator;
  return constrain(corrected, 0.0f, 1.0f);
}

void rgbToHslCorrected(float r, float g, float b, TCSHSLReading& out) {
  float maxVal = max(r, max(g, b));
  float minVal = min(r, min(g, b));
  float delta = maxVal - minVal;

  out.lightness = (maxVal + minVal) * 0.5f;

  if (delta <= 0.00001f) {
    out.hue = 0.0f;
    out.saturation = 0.0f;
    out.ok = true;
    return;
  }

  float denominator = 1.0f - fabsf(2.0f * out.lightness - 1.0f);

  if (denominator <= 0.00001f) {
    out.saturation = 0.0f;
  } else {
    out.saturation = delta / denominator;
  }

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

  out.hue = constrain(out.hue, 0.0f, 360.0f);
  out.saturation = constrain(out.saturation, 0.0f, 1.0f);
  out.lightness = constrain(out.lightness, 0.0f, 1.0f);
  out.ok = true;
}

void updateCorrectedColorFromRaw() {
  colorCorrectedOk = false;
  colorHsl = {};

  if (!colorRaw.ok || colorRaw.clear == 0) {
    return;
  }

  float r = correctColorChannel(colorRaw.red, COLOR_BLACK_R, COLOR_WHITE_R);
  float g = correctColorChannel(colorRaw.green, COLOR_BLACK_G, COLOR_WHITE_G);
  float b = correctColorChannel(colorRaw.blue, COLOR_BLACK_B, COLOR_WHITE_B);

  colorR8 = constrain((int)roundf(r * 255.0f), 0, 255);
  colorG8 = constrain((int)roundf(g * 255.0f), 0, 255);
  colorB8 = constrain((int)roundf(b * 255.0f), 0, 255);

  rgbToHslCorrected(r, g, b, colorHsl);

  colorCorrectedOk = true;
}

// ============================================================
// Actuator helpers
// ============================================================
void setFeedPump(bool on) {
  if (on && (alerts.fluidSystemLockout || alerts.feedEmptyLatched)) {
    on = false;
  }

  feedPumpOn = on;
  digitalWrite(PIN_PUMP_FEED, on ? HIGH : LOW);
}

void setWastePump(bool on) {
  if (on && alerts.fluidSystemLockout) {
    on = false;
  }

  wastePumpOn = on;
  digitalWrite(PIN_PUMP_WASTE, on ? HIGH : LOW);
}

void setOxygenPump(bool on) {
  oxygenPumpOn = on;
  digitalWrite(PIN_OXYGEN, on ? HIGH : LOW);
}

void setHeater(bool on) {
  if (on && alerts.heaterLockout) {
    on = false;
  }

  heaterOn = on;
  if (HEAT_RELAY_ACTIVE_LOW) {
    digitalWrite(PIN_HEAT_RELAY, on ? LOW : HIGH);
  } else {
    digitalWrite(PIN_HEAT_RELAY, on ? HIGH : LOW);
  }
}

void allActuatorsOff() {
  setFeedPump(false);
  setWastePump(false);
  setOxygenPump(false);
  setHeater(false);
}

// ============================================================
// MQTT publish helpers
// ============================================================
void publishStatus(const String& msg, bool retained = false) {
  if (!mqtt.connected()) {
    Serial.println("Status publish skipped: MQTT not connected");
    return;
  }
  bool ok = mqtt.publish(topicStatus.c_str(), msg.c_str(), retained);
  Serial.printf("Status publish %s\n", ok ? "OK" : "FAILED");
  Serial.println(msg);
}

void publishStatusSnapshot(const char* event, bool retained = false) {
  String payload = "{";
  payload += "\"device\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"ms\":" + String(millis()) + ",";
  payload += "\"event\":\"" + String(event) + "\",";
  payload += "\"debugMode\":" + String(DEBUG_MODE ? "true" : "false") + ",";
  payload += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  payload += "\"mqttConnected\":" + String(mqtt.connected() ? "true" : "false") + ",";
  payload += "\"feedPumpOn\":" + String(feedPumpOn ? "true" : "false") + ",";
  payload += "\"wastePumpOn\":" + String(wastePumpOn ? "true" : "false") + ",";
  payload += "\"oxygenOn\":" + String(oxygenPumpOn ? "true" : "false") + ",";
  payload += "\"heaterOn\":" + String(heaterOn ? "true" : "false") + ",";
  payload += "\"autoLevelControl\":" + String(autoLevelControl ? "true" : "false") + ",";
  payload += "\"autoHeaterControl\":" + String(autoHeaterControl ? "true" : "false") + ",";
  payload += "\"autoOxygenControl\":" + String(autoOxygenControl ? "true" : "false") + ",";
  payload += "\"oxygenRunSeconds\":" + String(oxygenRunSeconds, 1) + ",";
  payload += "\"oxygenWaitSeconds\":" + String(oxygenWaitSeconds, 1) + ",";
  payload += "\"feedEmptyAlert\":" + String(alerts.feedEmpty ? "true" : "false") + ",";
  payload += "\"wasteOverflowAlert\":" + String(alerts.wasteOverflow ? "true" : "false") + ",";
  payload += "\"mainJarTooFullAlert\":" + String(alerts.mainJarTooFull ? "true" : "false") + ",";
  payload += "\"tempSensorFault\":" + String(alerts.tempSensorFault ? "true" : "false") + ",";
  payload += "\"phSensorFault\":" + String(alerts.phSensorFault ? "true" : "false") + ",";
  payload += "\"colorSensorFault\":" + String(alerts.colorSensorFault ? "true" : "false") + ",";
  payload += "\"feedEmptyLatched\":" + String(alerts.feedEmptyLatched ? "true" : "false") + ",";
  payload += "\"wasteOverflowLatched\":" + String(alerts.wasteOverflowLatched ? "true" : "false") + ",";
  payload += "\"mainJarTooFullLatched\":" + String(alerts.mainJarTooFullLatched ? "true" : "false") + ",";
  payload += "\"tempSensorFaultLatched\":" + String(alerts.tempSensorFaultLatched ? "true" : "false") + ",";
  payload += "\"fluidSystemLockout\":" + String(alerts.fluidSystemLockout ? "true" : "false") + ",";
  payload += "\"heaterLockout\":" + String(alerts.heaterLockout ? "true" : "false");
  payload += "}";
  publishStatus(payload, retained);
}

void publishAlert(const char* code, const String& message) {
  if (!mqtt.connected()) return;
  String payload = "{";
  payload += "\"device\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"ms\":" + String(millis()) + ",";
  payload += "\"code\":\"" + String(code) + "\",";
  payload += "\"message\":\"" + message + "\"";
  payload += "}";
  mqtt.publish(topicAlerts.c_str(), payload.c_str(), false);
  Serial.println("Alert published:");
  Serial.println(payload);
}

void publishTelemetry() {
  if (!mqtt.connected()) return;

  String payload = "{";
  payload += "\"device\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"ms\":" + String(millis()) + ",";
  payload += "\"debugMode\":" + String(DEBUG_MODE ? "true" : "false") + ",";

  payload += "\"tempC\":" + ((std::isfinite(tempC) && tempSensor.isValid(tempC)) ? String(tempC, 2) : String("null")) + ",";
  payload += "\"ph\":" + (std::isfinite(phValue) ? String(phValue, 2) : String("null")) + ",";
  payload += "\"phVoltage\":" + (std::isfinite(phVoltage) ? String(phVoltage, 3) : String("null")) + ",";

  payload += "\"mainDistanceCm\":" + (usReadings[IDX_MAIN].ok ? String(usReadings[IDX_MAIN].cm, 2) : String("null")) + ",";
  payload += "\"wasteDistanceCm\":" + (usReadings[IDX_WASTE].ok ? String(usReadings[IDX_WASTE].cm, 2) : String("null")) + ",";
  payload += "\"feedDistanceCm\":" + (usReadings[IDX_FEED].ok ? String(usReadings[IDX_FEED].cm, 2) : String("null")) + ",";

  payload += "\"feedPumpOn\":" + String(feedPumpOn ? "true" : "false") + ",";
  payload += "\"wastePumpOn\":" + String(wastePumpOn ? "true" : "false") + ",";
  payload += "\"oxygenOn\":" + String(oxygenPumpOn ? "true" : "false") + ",";
  payload += "\"heaterOn\":" + String(heaterOn ? "true" : "false") + ",";

  payload += "\"autoLevelControl\":" + String(autoLevelControl ? "true" : "false") + ",";
  payload += "\"autoHeaterControl\":" + String(autoHeaterControl ? "true" : "false") + ",";
  payload += "\"autoOxygenControl\":" + String(autoOxygenControl ? "true" : "false") + ",";
  payload += "\"oxygenRunSeconds\":" + String(oxygenRunSeconds, 1) + ",";
  payload += "\"oxygenWaitSeconds\":" + String(oxygenWaitSeconds, 1) + ",";

  payload += "\"feedEmptyAlert\":" + String(alerts.feedEmpty ? "true" : "false") + ",";
  payload += "\"wasteOverflowAlert\":" + String(alerts.wasteOverflow ? "true" : "false") + ",";
  payload += "\"mainJarTooFullAlert\":" + String(alerts.mainJarTooFull ? "true" : "false") + ",";
  payload += "\"tempSensorFault\":" + String(alerts.tempSensorFault ? "true" : "false") + ",";
  payload += "\"phSensorFault\":" + String(alerts.phSensorFault ? "true" : "false") + ",";
  payload += "\"colorSensorFault\":" + String(alerts.colorSensorFault ? "true" : "false") + ",";
  payload += "\"feedEmptyLatched\":" + String(alerts.feedEmptyLatched ? "true" : "false") + ",";
  payload += "\"wasteOverflowLatched\":" + String(alerts.wasteOverflowLatched ? "true" : "false") + ",";
  payload += "\"mainJarTooFullLatched\":" + String(alerts.mainJarTooFullLatched ? "true" : "false") + ",";
  payload += "\"tempSensorFaultLatched\":" + String(alerts.tempSensorFaultLatched ? "true" : "false") + ",";
  payload += "\"fluidSystemLockout\":" + String(alerts.fluidSystemLockout ? "true" : "false") + ",";
  payload += "\"heaterLockout\":" + String(alerts.heaterLockout ? "true" : "false") + ",";

  payload += "\"mainDistanceTargetCm\":" + String(params.mainDistanceTargetCm, 2) + ",";
  payload += "\"mainDistanceBandCm\":" + String(params.mainDistanceBandCm, 2) + ",";
  payload += "\"mainTooFullDistanceCm\":" + String(params.mainTooFullDistanceCm, 2) + ",";
  payload += "\"feedEmptyDistanceCm\":" + String(params.feedEmptyDistanceCm, 2) + ",";
  payload += "\"wasteOverflowDistanceCm\":" + String(params.wasteOverflowDistanceCm, 2) + ",";
  payload += "\"tempLowC\":" + String(params.tempLowC, 2) + ",";
  payload += "\"tempHighC\":" + String(params.tempHighC, 2) + ",";

  payload += "\"phCalVoltageA\":" + String(phSensor.getCalVoltageA(), 3) + ",";
  payload += "\"phCalVoltageB\":" + String(phSensor.getCalVoltageB(), 3) + ",";

  payload += "\"colorRaw\":{";
  payload += "\"r\":" + String(colorRaw.red) + ",";
  payload += "\"g\":" + String(colorRaw.green) + ",";
  payload += "\"b\":" + String(colorRaw.blue) + ",";
  payload += "\"c\":" + String(colorRaw.clear) + "},";

  payload += "\"colorHsl\":{";
  payload += "\"h\":" + (colorHsl.ok ? String(colorHsl.hue, 1) : String("null")) + ",";
  payload += "\"s\":" + (colorHsl.ok ? String(colorHsl.saturation, 3) : String("null")) + ",";
  payload += "\"l\":" + (colorHsl.ok ? String(colorHsl.lightness, 3) : String("null"));
  payload += "}";

  payload += "}";

  bool ok = mqtt.publish(topicTelemetry.c_str(), payload.c_str(), false);
  Serial.printf("Telemetry publish %s\n", ok ? "OK" : "FAILED");
  Serial.println(payload);
}

void publishCommandAck(const String& payloadIn) {
  if (!mqtt.connected()) return;
  String ack = "{";
  ack += "\"ok\":true,";
  ack += "\"device\":\"" + String(DEVICE_ID) + "\",";
  ack += "\"received\":" + payloadIn;
  ack += "}";
  mqtt.publish(topicCommandAck.c_str(), ack.c_str(), false);
  Serial.println("Command ACK:");
  Serial.println(ack);
}

void publishConfigAck() {
  if (!mqtt.connected()) return;
  String ack = "{";
  ack += "\"ok\":true,";
  ack += "\"mainDistanceTargetCm\":" + String(params.mainDistanceTargetCm, 2) + ",";
  ack += "\"mainDistanceBandCm\":" + String(params.mainDistanceBandCm, 2) + ",";
  ack += "\"mainTooFullDistanceCm\":" + String(params.mainTooFullDistanceCm, 2) + ",";
  ack += "\"feedEmptyDistanceCm\":" + String(params.feedEmptyDistanceCm, 2) + ",";
  ack += "\"wasteOverflowDistanceCm\":" + String(params.wasteOverflowDistanceCm, 2) + ",";
  ack += "\"tempLowC\":" + String(params.tempLowC, 2) + ",";
  ack += "\"tempHighC\":" + String(params.tempHighC, 2) + ",";
  ack += "\"oxygenRunSeconds\":" + String(oxygenRunSeconds, 1) + ",";
  ack += "\"oxygenWaitSeconds\":" + String(oxygenWaitSeconds, 1) + ",";
  ack += "\"phCalVoltageA\":" + String(phSensor.getCalVoltageA(), 3) + ",";
  ack += "\"phCalVoltageB\":" + String(phSensor.getCalVoltageB(), 3);
  ack += "}";
  mqtt.publish(topicConfigAck.c_str(), ack.c_str(), false);
  Serial.println("Config ACK:");
  Serial.println(ack);
}

// ============================================================
// Debug sensor simulation
// ============================================================
void generateDebugReadings() {
  float t = millis() / 1000.0f;

  // Simulated distances
  usReadings[IDX_MAIN].ok = true;
  usReadings[IDX_MAIN].cm = 10.0f + 1.8f * sinf(t * 0.35f);

  usReadings[IDX_WASTE].ok = true;
  usReadings[IDX_WASTE].cm = 8.0f + 4.0f * sinf(t * 0.20f + 1.2f);

  usReadings[IDX_FEED].ok = true;
  usReadings[IDX_FEED].cm = 9.0f + 7.0f * sinf(t * 0.18f + 2.1f);

  // Simulated temperature
  tempC = 25.0f + 2.5f * sinf(t * 0.25f);

  // Simulated pH
  phValue = 6.80f + 0.45f * sinf(t * 0.17f);
  phVoltage = 2.80f - 0.05f * sinf(t * 0.17f);

  // Simulated color
  colorRaw.red = 120 + (int)(20.0f * (1.0f + sinf(t * 0.4f)));
  colorRaw.green = 90 + (int)(15.0f * (1.0f + sinf(t * 0.3f + 1.0f)));
  colorRaw.blue = 40 + (int)(10.0f * (1.0f + sinf(t * 0.5f + 2.0f)));
  colorRaw.clear = colorRaw.red + colorRaw.green + colorRaw.blue;
  colorRaw.ok = (colorRaw.clear > 0);

  colorHsl.ok = true;
  colorHsl.hue = fmodf(30.0f + t * 4.0f, 360.0f);
  colorHsl.saturation = 0.55f + 0.10f * sinf(t * 0.22f);
  colorHsl.lightness = 0.38f + 0.05f * sinf(t * 0.31f);

  // Occasionally inject a fake temperature fault so alerts are visible
  if (((millis() / 20000UL) % 2UL) == 1UL) {
    tempC = NAN;
    colorHsl.ok = false;
    phValue = NAN;
  }
}

// ============================================================
// Safety + controls
// ============================================================

void recomputeSystemLockouts() {
  alerts.fluidSystemLockout =
      alerts.wasteOverflowLatched ||
      alerts.mainJarTooFullLatched;

  alerts.heaterLockout = alerts.tempSensorFaultLatched;
}

bool isWasteOverflowNow() {
  const float wasteCm = usReadings[IDX_WASTE].cm;
  return usReadings[IDX_WASTE].ok &&
         isFinitePositive(wasteCm) &&
         wasteCm <= params.wasteOverflowDistanceCm;
}

bool isFeedEmptyNow() {
  const float feedCm = usReadings[IDX_FEED].cm;
  return usReadings[IDX_FEED].ok &&
         isFinitePositive(feedCm) &&
         feedCm >= params.feedEmptyDistanceCm;
}

bool isMainJarTooFullNow() {
  const float mainCm = usReadings[IDX_MAIN].cm;
  return usReadings[IDX_MAIN].ok &&
         isFinitePositive(mainCm) &&
         mainCm <= params.mainTooFullDistanceCm;
}

bool isTempSensorFaultNow() {
  return !std::isfinite(tempC) || !tempSensor.isValid(tempC);
}

bool isColorSensorFaultNow() {
  // Live-only color sensor warning. Do not latch because it does not directly control a dangerous actuator.
  return !colorRaw.ok || colorRaw.clear == 0;
}

void clearFluidLockoutIfSafe() {
  if (isWasteOverflowNow()) {
    publishAlert("LOCKOUT_CLEAR_REFUSED", "Cannot clear fluid lockout: waste jar still appears full.");
    return;
  }

  if (isMainJarTooFullNow()) {
    publishAlert("LOCKOUT_CLEAR_REFUSED", "Cannot clear fluid lockout: main jar still appears too full.");
    return;
  }

  if (isFeedEmptyNow()) {
    publishAlert("LOCKOUT_CLEAR_REFUSED", "Cannot clear fluid lockout: feed jar still appears empty.");
    return;
  }

  alerts.wasteOverflowLatched = false;
  alerts.mainJarTooFullLatched = false;
  alerts.feedEmptyLatched = false;
  recomputeSystemLockouts();

  setFeedPump(false);
  setWastePump(false);

  publishAlert("FLUID_LOCKOUT_CLEARED", "Fluid lockout cleared by user. Pumps are unlocked.");
  publishStatusSnapshot("lockout_cleared", true);
}

void clearFeedEmptyAlertIfSafe() {
  if (isFeedEmptyNow()) {
    publishAlert("FEED_EMPTY_STILL_ACTIVE", "Cannot clear feed empty alert. Feed jar still appears empty.");
    return;
  }

  alerts.feedEmptyLatched = false;
  recomputeSystemLockouts();

  setFeedPump(false);

  publishAlert("FEED_EMPTY_CLEARED", "Feed empty alert cleared by user. Feed pump is unlocked.");
  publishStatusSnapshot("feed_empty_cleared", true);
}

void clearWasteOverflowAlertIfSafe() {
  if (isWasteOverflowNow()) {
    publishAlert("WASTE_OVERFLOW_STILL_ACTIVE", "Cannot clear waste overflow alert. Waste jar still appears full.");
    return;
  }

  alerts.wasteOverflowLatched = false;
  recomputeSystemLockouts();

  setFeedPump(false);
  setWastePump(false);

  publishAlert("WASTE_OVERFLOW_CLEARED", "Waste overflow alert cleared by user.");
  publishStatusSnapshot("waste_overflow_cleared", true);
}

void clearMainJarTooFullAlertIfSafe() {
  if (isMainJarTooFullNow()) {
    publishAlert("MAIN_JAR_TOO_FULL_STILL_ACTIVE", "Cannot clear main jar too-full alert. Main jar still appears too full.");
    return;
  }

  alerts.mainJarTooFullLatched = false;
  recomputeSystemLockouts();

  setFeedPump(false);
  setWastePump(false);

  publishAlert("MAIN_JAR_TOO_FULL_CLEARED", "Main jar too-full alert cleared by user.");
  publishStatusSnapshot("main_jar_too_full_cleared", true);
}

void clearTempSensorFaultIfSafe() {
  if (isTempSensorFaultNow()) {
    publishAlert("TEMP_SENSOR_FAULT_STILL_ACTIVE", "Cannot clear heater lockout. Temperature sensor is still invalid.");
    return;
  }

  alerts.tempSensorFaultLatched = false;
  recomputeSystemLockouts();

  setHeater(false);

  publishAlert("TEMP_SENSOR_LOCKOUT_CLEARED", "Temperature sensor fault cleared by user. Heater is unlocked.");
  publishStatusSnapshot("temp_lockout_cleared", true);
}

void evaluateAlertsAndSafety() {
  const bool prevFeedEmpty = alerts.feedEmpty;
  const bool prevWasteOverflow = alerts.wasteOverflow;
  const bool prevMainJarTooFull = alerts.mainJarTooFull;
  const bool prevTempFault = alerts.tempSensorFault;
  const bool prevPhFault = alerts.phSensorFault;
  const bool prevColorFault = alerts.colorSensorFault;

  alerts.feedEmpty = isFeedEmptyNow();
  alerts.wasteOverflow = isWasteOverflowNow();
  alerts.mainJarTooFull = isMainJarTooFullNow();
  alerts.tempSensorFault = isTempSensorFaultNow();
  alerts.phSensorFault =
      !std::isfinite(phVoltage) ||
      phVoltage < 0.05f ||
      phVoltage > 3.25f ||
      !std::isfinite(phValue);
  alerts.colorSensorFault = isColorSensorFaultNow();

  if (alerts.feedEmpty && !alerts.feedEmptyLatched) {
    alerts.feedEmptyLatched = true;
    setFeedPump(false);
    publishAlert("FEED_EMPTY", "Feed jar is empty. Feed pump locked OFF until user clears alert.");
  }

  if (alerts.wasteOverflow && !alerts.wasteOverflowLatched) {
    alerts.wasteOverflowLatched = true;
    recomputeSystemLockouts();
    setFeedPump(false);
    setWastePump(false);
    publishAlert("WASTE_OVERFLOW", "Waste jar near overflow. Fluid pumps locked OFF until user clears alert.");
  }

  if (alerts.mainJarTooFull && !alerts.mainJarTooFullLatched) {
    alerts.mainJarTooFullLatched = true;
    recomputeSystemLockouts();
    setFeedPump(false);
    setWastePump(false);
    publishAlert("MAIN_JAR_TOO_FULL", "Main jar appears too full. Fluid pumps locked OFF until user clears alert.");
  }

  if (alerts.tempSensorFault && !alerts.tempSensorFaultLatched) {
    alerts.tempSensorFaultLatched = true;
    recomputeSystemLockouts();
    setHeater(false);
    publishAlert("TEMP_SENSOR_FAULT", "Temperature sensor invalid. Heater locked OFF until user clears alert.");
  }

  // Non-latched informational alerts for pH.
  if (alerts.phSensorFault && !prevPhFault) {
    publishAlert("PH_SENSOR_FAULT", "pH sensor reading invalid or disconnected.");
  }
  if (!alerts.phSensorFault && prevPhFault) {
    publishAlert("PH_SENSOR_OK", "pH sensor reading restored.");
  }

  // Non-latched informational alerts for RGB/color sensor.
  if (alerts.colorSensorFault && !prevColorFault) {
    publishAlert("COLOR_SENSOR_FAULT", "RGB color sensor reading invalid. Check sensor wiring, lighting, or I2C connection.");
  }
  if (!alerts.colorSensorFault && prevColorFault) {
    publishAlert("COLOR_SENSOR_OK", "RGB color sensor reading restored.");
  }

  // Informational live-state changes. These do not clear latches.
  if (!alerts.feedEmpty && prevFeedEmpty) {
    publishAlert("FEED_EMPTY_SENSOR_SAFE", "Feed jar sensor is no longer reporting empty. User clear is still required if latched.");
  }
  if (!alerts.wasteOverflow && prevWasteOverflow) {
    publishAlert("WASTE_OVERFLOW_SENSOR_SAFE", "Waste jar sensor is no longer reporting overflow. User clear is still required if latched.");
  }
  if (!alerts.mainJarTooFull && prevMainJarTooFull) {
    publishAlert("MAIN_JAR_TOO_FULL_SENSOR_SAFE", "Main jar sensor is no longer reporting too full. User clear is still required if latched.");
  }
  if (!alerts.tempSensorFault && prevTempFault) {
    publishAlert("TEMP_SENSOR_SENSOR_OK", "Temperature sensor reading restored. User clear is still required if heater lockout is latched.");
  }

  recomputeSystemLockouts();
}

void runOxygenTimerControl() {
  if (!autoOxygenControl) {
    oxygenCycleRunning = false;
    return;
  }

  const unsigned long now = millis();

  unsigned long runMs = (unsigned long)(oxygenRunSeconds * 1000.0f);
  unsigned long waitMs = (unsigned long)(oxygenWaitSeconds * 1000.0f);

  // Safety minimums so bad website input does not break the timer.
  if (runMs < 1000UL) runMs = 1000UL;
  if (waitMs < 1000UL) waitMs = 1000UL;

  // First time auto oxygen is enabled, start by turning oxygen ON.
  if (!oxygenCycleRunning && !oxygenPumpOn) {
    setOxygenPump(true);
    oxygenCycleRunning = true;
    oxygenCycleStateStartMs = now;
    return;
  }

  // If oxygen is currently ON, turn it OFF after run duration.
  if (oxygenPumpOn) {
    if (now - oxygenCycleStateStartMs >= runMs) {
      setOxygenPump(false);
      oxygenCycleStateStartMs = now;
    }
  }

  // If oxygen is currently OFF, turn it ON after wait duration.
  else {
    if (now - oxygenCycleStateStartMs >= waitMs) {
      setOxygenPump(true);
      oxygenCycleStateStartMs = now;
    }
  }
}

void runAutoControls() {
  const float mainCm = usReadings[IDX_MAIN].cm;

  if (alerts.fluidSystemLockout) {
    setFeedPump(false);
    setWastePump(false);
  } else if (autoLevelControl && usReadings[IDX_MAIN].ok && isFinitePositive(mainCm)) {
    const float lowBound = params.mainDistanceTargetCm - params.mainDistanceBandCm;
    const float highBound = params.mainDistanceTargetCm + params.mainDistanceBandCm;

    if (mainCm > highBound) {
      setWastePump(false);
      setFeedPump(!alerts.feedEmptyLatched);
    } else if (mainCm < lowBound) {
      setFeedPump(false);
      setWastePump(true);
    } else {
      setFeedPump(false);
      setWastePump(false);
    }
  }

  if (autoHeaterControl) {
    if (alerts.heaterLockout || !std::isfinite(tempC) || !tempSensor.isValid(tempC)) {
      setHeater(false);
    } else if (tempC <= params.tempLowC) {
      setHeater(true);
    } else if (tempC >= params.tempHighC) {
      setHeater(false);
    }
  }

  runOxygenTimerControl();
}

// ============================================================
// Command/config handling
// ============================================================
void handleCommand(const String& payload) {
  if (payload.indexOf("\"feedPumpOn\"") >= 0) {
    autoLevelControl = false;
    setFeedPump(extractBool(payload, "feedPumpOn", feedPumpOn));
  }

  if (payload.indexOf("\"wastePumpOn\"") >= 0) {
    autoLevelControl = false;
    setWastePump(extractBool(payload, "wastePumpOn", wastePumpOn));
  }

  if (payload.indexOf("\"heaterOn\"") >= 0) {
    autoHeaterControl = false;
    setHeater(extractBool(payload, "heaterOn", heaterOn));
  }

  if (payload.indexOf("\"oxygenOn\"") >= 0) {
    autoOxygenControl = false;
    oxygenCycleRunning = false;
    setOxygenPump(extractBool(payload, "oxygenOn", oxygenPumpOn));
  }

  if (payload.indexOf("\"autoOxygenControl\"") >= 0) {
    bool newAutoOxygen = extractBool(payload, "autoOxygenControl", autoOxygenControl);

    if (newAutoOxygen != autoOxygenControl) {
      autoOxygenControl = newAutoOxygen;
      oxygenCycleRunning = false;
      oxygenCycleStateStartMs = millis();

      if (!autoOxygenControl) {
        setOxygenPump(false);
      }
    }
  }

  if (extractBool(payload, "clearFluidLockout", false)) {
    clearFluidLockoutIfSafe();
  }

  if (extractBool(payload, "clearFeedEmptyAlert", false)) {
    clearFeedEmptyAlertIfSafe();
  }

  if (extractBool(payload, "clearWasteOverflowAlert", false)) {
    clearWasteOverflowAlertIfSafe();
  }

  if (extractBool(payload, "clearMainJarTooFullAlert", false)) {
    clearMainJarTooFullAlertIfSafe();
  }

  if (extractBool(payload, "clearTempSensorFault", false)) {
    clearTempSensorFaultIfSafe();
  }

  autoLevelControl = extractBool(payload, "autoLevelControl", autoLevelControl);
  autoHeaterControl = extractBool(payload, "autoHeaterControl", autoHeaterControl);

  if (extractBool(payload, "allOff", false)) {
    autoLevelControl = false;
    autoHeaterControl = false;
    autoOxygenControl = false;
    oxygenCycleRunning = false;
    allActuatorsOff();
  }

  evaluateAlertsAndSafety();
  publishStatusSnapshot("command_applied", true);
  publishCommandAck(payload);
}

void handleConfig(const String& payload) {
  params.mainDistanceTargetCm = extractFloat(payload, "mainDistanceTargetCm", params.mainDistanceTargetCm);
  params.mainDistanceBandCm = extractFloat(payload, "mainDistanceBandCm", params.mainDistanceBandCm);
  params.mainTooFullDistanceCm = extractFloat(payload, "mainTooFullDistanceCm", params.mainTooFullDistanceCm);
  params.feedEmptyDistanceCm = extractFloat(payload, "feedEmptyDistanceCm", params.feedEmptyDistanceCm);
  params.wasteOverflowDistanceCm = extractFloat(payload, "wasteOverflowDistanceCm", params.wasteOverflowDistanceCm);
  params.tempLowC = extractFloat(payload, "tempLowC", params.tempLowC);
  params.tempHighC = extractFloat(payload, "tempHighC", params.tempHighC);

  oxygenRunSeconds = extractFloat(payload, "oxygenRunSeconds", oxygenRunSeconds);
  oxygenWaitSeconds = extractFloat(payload, "oxygenWaitSeconds", oxygenWaitSeconds);

  if (params.mainDistanceBandCm < 0.1f) params.mainDistanceBandCm = 0.1f;
  if (params.mainTooFullDistanceCm < 0.5f) params.mainTooFullDistanceCm = 0.5f;
  if (params.feedEmptyDistanceCm < 1.0f) params.feedEmptyDistanceCm = 1.0f;
  if (params.wasteOverflowDistanceCm < 0.5f) params.wasteOverflowDistanceCm = 0.5f;
  if (params.tempHighC < params.tempLowC + 0.2f) params.tempHighC = params.tempLowC + 0.2f;
  if (oxygenRunSeconds < 1.0f) oxygenRunSeconds = 1.0f;
  if (oxygenWaitSeconds < 1.0f) oxygenWaitSeconds = 1.0f;

  float newPhCalVoltageA = extractFloat(payload, "phCalVoltageA", phSensor.getCalVoltageA());
  float newPhCalVoltageB = extractFloat(payload, "phCalVoltageB", phSensor.getCalVoltageB());

  if (newPhCalVoltageA > 0.0f && newPhCalVoltageB > 0.0f &&
      fabs(newPhCalVoltageA - newPhCalVoltageB) > 0.001f) {
    phSensor.setCalibrationPoints(4.01f, newPhCalVoltageA, 9.16f, newPhCalVoltageB);
    phVoltage = phSensor.getLastVoltage();
    phValue = phSensor.readPH();
  }

  publishStatusSnapshot("config_applied", true);
  publishConfigAck();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) msg += static_cast<char>(payload[i]);

  Serial.print("MQTT RX topic: ");
  Serial.println(topic);
  Serial.println(msg);

  if (String(topic) == topicCommandSet) {
    handleCommand(msg);
  } else if (String(topic) == topicConfigSet) {
    handleConfig(msg);
  }
}

// ============================================================
// Wi-Fi / MQTT
// ============================================================
bool waitForWiFi(unsigned long timeoutMs) {
  const unsigned long start = millis();
  Serial.print("Wi-Fi connecting");
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(400);
    Serial.print('.');
  }
  const bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok) {
    Serial.printf("\nWi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("\nWi-Fi connect timeout, status=%d\n", (int)WiFi.status());
  }
  return ok;
}

void beginSchoolWiFiEnterprise() {
  Serial.printf("Connecting WPA2-Enterprise SSID=%s identity=%s\n", WIFI_SSID, WIFI_IDENTITY);
  const char* entPass = (strlen(WIFI_USER_PASSWORD) > 0) ? WIFI_USER_PASSWORD : WIFI_PASSWORD;

#if defined(HAS_ESP_EAP_CLIENT)
  esp_wifi_sta_enterprise_disable();
  esp_eap_client_clear_identity();
  esp_eap_client_clear_username();
  esp_eap_client_clear_password();
  esp_eap_client_set_identity((const uint8_t*)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
  esp_eap_client_set_username((const uint8_t*)WIFI_USERNAME, strlen(WIFI_USERNAME));
  esp_eap_client_set_password((const uint8_t*)entPass, strlen(entPass));
  esp_eap_client_set_ca_cert(nullptr, 0);
  esp_wifi_sta_enterprise_enable();
  WiFi.begin(WIFI_SSID);

#elif defined(HAS_ESP_WPA2)
  esp_wifi_sta_wpa2_ent_disable();
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
  esp_wifi_sta_wpa2_ent_set_username((uint8_t*)WIFI_USERNAME, strlen(WIFI_USERNAME));
  esp_wifi_sta_wpa2_ent_set_password((uint8_t*)entPass, strlen(entPass));
  esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT();
  esp_wifi_sta_wpa2_ent_enable(&config);
  WiFi.begin(WIFI_SSID);

#else
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
}

void beginSchoolWiFiPersonal() {
  Serial.printf("Connecting WPA2-Personal SSID=%s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectSchoolWiFi() {
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (WIFI_USE_ENTERPRISE) {
    beginSchoolWiFiEnterprise();
    if (!waitForWiFi(25000UL)) {
      WiFi.disconnect(true);
      delay(100);
      beginSchoolWiFiPersonal();
      waitForWiFi(20000UL);
    }
  } else {
    beginSchoolWiFiPersonal();
    waitForWiFi(20000UL);
  }
}

bool connectMqtt() {
  if (mqtt.connected()) return true;
  String clientId = String("progress-") + DEVICE_ID + "-" + String((uint32_t)esp_random(), HEX);

  Serial.printf("Connecting MQTT %s:%u ...\n", MQTT_HOST, MQTT_PORT);
  if (!mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
    Serial.printf("MQTT connect failed, state=%d\n", mqtt.state());
    return false;
  }

  mqtt.subscribe(topicCommandSet.c_str());
  mqtt.subscribe(topicConfigSet.c_str());

  publishStatusSnapshot("online", true);
  Serial.println("MQTT connected.");
  return true;
}

void setupTopics() {
  String base = String("kombucha/") + DEVICE_ID;
  topicTelemetry = base + "/telemetry";
  topicStatus = base + "/status";
  topicAlerts = base + "/alerts";
  topicCommandSet = base + "/command/set";
  topicCommandAck = base + "/command/ack";
  topicConfigSet = base + "/config/set";
  topicConfigAck = base + "/config/ack";
}

void setupActuatorPins() {
  pinMode(PIN_PUMP_FEED, OUTPUT);
  pinMode(PIN_PUMP_WASTE, OUTPUT);
  pinMode(PIN_OXYGEN, OUTPUT);
  pinMode(PIN_HEAT_RELAY, OUTPUT);
  allActuatorsOff();
}

void initSensorsIfNeeded() {
  if (DEBUG_MODE) {
    Serial.println("DEBUG_MODE enabled: skipping real sensor initialization");
    for (int i = 0; i < US_COUNT; i++) {
      usReadings[i].ok = false;
      usReadings[i].cm = NAN;
    }
    phValue = NAN;
    phVoltage = NAN;
    tempC = NAN;
    colorRaw = {};
    colorHsl.ok = false;
    return;
  }

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  usArray.begin();
  usArray.setTiming(26000UL, 200UL);
  tempSensor.begin();
  colorSensor.begin();
  phSensor.begin();
  Serial.println("Real sensor initialization complete");
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("Progress demo booting...");

  connectSchoolWiFi();

  setupTopics();
  setupActuatorPins();
  initSensorsIfNeeded();

  

  secureClient.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(2048);
}

void loop() {
  static unsigned long lastSensorMs = 0;
  static unsigned long lastTelemetryMs = 0;
  static unsigned long lastStatusMs = 0;
  static unsigned long lastMqttRetryMs = 0;

  if (WiFi.status() != WL_CONNECTED) {
    connectSchoolWiFi();
  }

  if (!mqtt.connected()) {
    const unsigned long now = millis();
    if (now - lastMqttRetryMs >= MQTT_RETRY_MS) {
      lastMqttRetryMs = now;
      connectMqtt();
    }
  } else {
    mqtt.loop();
  }

  const unsigned long now = millis();

  if (now - lastSensorMs >= SENSOR_PERIOD_MS) {
    lastSensorMs = now;

    if (DEBUG_MODE) {
      generateDebugReadings();
    } else {
      usArray.readAll(usReadings);
      tempC = tempSensor.readCBlocking();
      readColorSensorWithRecovery();
      phVoltage = phSensor.readVoltage();
      phValue = phSensor.readPH();
    }

    evaluateAlertsAndSafety();
    runAutoControls();
  }

  if (now - lastTelemetryMs >= TELEMETRY_PERIOD_MS) {
    lastTelemetryMs = now;
    publishTelemetry();

    // Make status easy to observe during debugging
    publishStatusSnapshot("tick", true);
  }

  if (now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    publishStatusSnapshot("heartbeat", true);
  }
}
