#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "UltrasonicArray.h"
#include "PumpController.h"

class KombuchaDashboardApp {
public:
  void begin();
  void loop();

private:
  struct SensorSnapshot {
    unsigned long timestampMs = 0;
    float levelCm[2] = {NAN, NAN};
    bool levelOk[2] = {false, false};
  };

  struct ActuatorState {
    bool oxygenOn = false;
    bool heaterOn = false;
    int pumpDutyA = 0;
    int pumpDutyB = 0;
    bool pumpAutoA = true;
    bool pumpAutoB = true;
  };

  void addLog(const String& msg);
  void connectWiFi();
  void setupWebRoutes();

  void setOxygen(bool on);
  void setHeater(bool on);
  void setPumpDutyA(int duty, bool manualCommand);
  void setPumpDutyB(int duty, bool manualCommand);

  void updateSensors();
  void applyPumpAutopilot();

  String boolToJson(bool value);
  String makeStateJson();
  String makeLogJson();

  void handleRoot();
  void handleApiState();
  void handleApiLogs();
  void handleApiControl();

  // Wi-Fi config
  const char* _wifiSsid = "YOUR_WIFI_SSID";
  const char* _wifiPassword = "YOUR_WIFI_PASSWORD";

  // Timing
  static constexpr unsigned long SENSOR_UPDATE_MS = 1000UL;
  static constexpr size_t STATUS_LOG_MAX = 40;
  static constexpr unsigned long MANUAL_OVERRIDE_MS = 120000UL;

  // Autopilot thresholds with hysteresis (cm)
  static constexpr float PUMP_A_ON_BELOW_CM = 18.0f;
  static constexpr float PUMP_A_OFF_ABOVE_CM = 22.0f;
  static constexpr float PUMP_B_ON_BELOW_CM = 18.0f;
  static constexpr float PUMP_B_OFF_ABOVE_CM = 22.0f;

  // Pins and constants
  static constexpr int TRIG_PIN = 4;
  static const int ECHO_PINS[2];
  static constexpr int LEVEL_SENSOR_COUNT = 2;
  static constexpr int PUMP_A_PIN = 18;
  static constexpr int PUMP_B_PIN = 19;
  static constexpr int OXYGEN_RELAY_PIN = 26;
  static constexpr int HEATER_RELAY_PIN = 27;

  static constexpr int DEFAULT_PUMP_DUTY = 140;

  UltrasonicArray _us{TRIG_PIN, ECHO_PINS, LEVEL_SENSOR_COUNT};
  PumpController _pumps{PUMP_A_PIN, PUMP_B_PIN};

  WebServer _server{80};
  SensorSnapshot _currentSensors;
  ActuatorState _actuatorState;

  String _eventLog[STATUS_LOG_MAX];
  size_t _eventWriteIndex = 0;
  size_t _eventCount = 0;

  unsigned long _lastSensorUpdateMs = 0;
  unsigned long _manualOverrideUntilA = 0;
  unsigned long _manualOverrideUntilB = 0;
};
