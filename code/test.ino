#include <Arduino.h>

#include "TempSensorDS18B20.h"
#include "UltrasonicArray.h"

// -----------------------------------------------------------------------------
// test.ino
// Semi-automatic bring-up sketch
// Sensors:
// - 3x Ultrasonic (shared TRIG 14, ECHO 27/26/25)
// - DS18B20 temperature sensor (pin 4)
//
// Actuators:
// - Relay (heater control) pin 17
// - Peristaltic pump A pin 18 (fill/main-jar in)
// - Peristaltic pump B pin 19 (drain/main-jar out)
// - Oxygen pin 16 kept for manual tests only
// -----------------------------------------------------------------------------

constexpr int TRIG_PIN = 14;
constexpr int ECHO_PINS[] = {27, 26, 25};
constexpr int SENSOR_COUNT = sizeof(ECHO_PINS) / sizeof(ECHO_PINS[0]);
constexpr int MAIN_JAR_SENSOR_INDEX = 2;  // ECHO pin 25

constexpr int OXYGEN_PIN = 16;
constexpr int RELAY_PIN = 17;
constexpr int PUMP_A_PIN = 18;
constexpr int PUMP_B_PIN = 19;
constexpr int TEMP_SENSOR_PIN = 4;

// Set true for relay modules that turn ON with LOW input.
constexpr bool RELAY_ACTIVE_LOW = true;

// Auto control thresholds
constexpr float TARGET_TEMP_C = 27.0f;
constexpr float MAIN_JAR_LOW_CM = 6.0f;   // single-digit range low bound
constexpr float MAIN_JAR_HIGH_CM = 9.0f;  // single-digit range high bound

constexpr unsigned long AUTO_PERIOD_MS = 700UL;

UltrasonicArray us(TRIG_PIN, ECHO_PINS, SENSOR_COUNT);
TempSensorDS18B20 tempSensor(TEMP_SENSOR_PIN);

bool autoMode = true;

bool oxygenOn = false;
bool relayOn = false;
bool pumpAOn = false;
bool pumpBOn = false;

unsigned long lastAutoMs = 0;

void writeOutput(int pin, bool on) {
  digitalWrite(pin, on ? HIGH : LOW);
}

void writeRelay(bool on) {
  int level = on ? (RELAY_ACTIVE_LOW ? LOW : HIGH)
                 : (RELAY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(RELAY_PIN, level);
}

void setOxygen(bool on) {
  if (oxygenOn == on) return;
  oxygenOn = on;
  writeOutput(OXYGEN_PIN, oxygenOn);
  Serial.print("oxygen -> ");
  Serial.println(oxygenOn ? "ON" : "OFF");
}

void setRelay(bool on) {
  if (relayOn == on) return;
  relayOn = on;
  writeRelay(relayOn);
  Serial.print("heater relay -> ");
  Serial.println(relayOn ? "ON" : "OFF");
}

void setPumpA(bool on) {
  if (pumpAOn == on) return;
  pumpAOn = on;
  writeOutput(PUMP_A_PIN, pumpAOn);
  Serial.print("pump A (fill) -> ");
  Serial.println(pumpAOn ? "ON" : "OFF");
}

void setPumpB(bool on) {
  if (pumpBOn == on) return;
  pumpBOn = on;
  writeOutput(PUMP_B_PIN, pumpBOn);
  Serial.print("pump B (drain) -> ");
  Serial.println(pumpBOn ? "ON" : "OFF");
}

void printStates() {
  Serial.print("States -> mode:");
  Serial.print(autoMode ? "AUTO" : "MANUAL");
  Serial.print(" oxygen:");
  Serial.print(oxygenOn ? "ON" : "OFF");
  Serial.print(" relay:");
  Serial.print(relayOn ? "ON" : "OFF");
  Serial.print(" pumpA:");
  Serial.print(pumpAOn ? "ON" : "OFF");
  Serial.print(" pumpB:");
  Serial.println(pumpBOn ? "ON" : "OFF");
}

void setAllOff() {
  setOxygen(false);
  setRelay(false);
  setPumpA(false);
  setPumpB(false);
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  h = help");
  Serial.println("  s = print actuator states");
  Serial.println("  u = print ultrasonic readings once");
  Serial.println("  t = read temperature sensor once (DS18B20 pin 4)");
  Serial.println("  m = toggle AUTO/MANUAL mode");
  Serial.println("  o = toggle oxygen pump pin 16 (manual test)");
  Serial.println("  r = toggle relay pin 17 (manual only)");
  Serial.println("  a = toggle peristaltic pump A pin 18 (manual only)");
  Serial.println("  b = toggle peristaltic pump B pin 19 (manual only)");
  Serial.println("  x = all outputs OFF");
}

bool readMainJarLevelCm(float& outCm) {
  USReading r = us.readOne(MAIN_JAR_SENSOR_INDEX);
  if (!r.ok) return false;
  outCm = r.cm;
  return true;
}

void printUltrasonicOnce() {
  Serial.print("[cm] ECHO27=");
  USReading r0 = us.readOne(0);
  if (r0.ok) Serial.print(r0.cm, 1); else Serial.print("timeout");

  Serial.print("  ECHO26=");
  USReading r1 = us.readOne(1);
  if (r1.ok) Serial.print(r1.cm, 1); else Serial.print("timeout");

  Serial.print("  ECHO25(main)=");
  USReading r2 = us.readOne(2);
  if (r2.ok) Serial.print(r2.cm, 1); else Serial.print("timeout");

  Serial.println();
}

bool readTemperatureC(float& outTC) {
  outTC = tempSensor.readCBlocking();
  return tempSensor.isValid(outTC);
}

void printTemperatureOnce() {
  float tC = NAN;
  if (readTemperatureC(tC)) {
    Serial.print("[temp] ");
    Serial.print(tC, 2);
    Serial.println(" C");
  } else {
    Serial.println("[temp] invalid/disconnected (check wiring + 4.7k pull-up)");
  }
}

void runAutoControl() {
  const unsigned long now = millis();
  if ((now - lastAutoMs) < AUTO_PERIOD_MS) return;
  lastAutoMs = now;

  // 1) Temperature heater control
  float tC = NAN;
  const bool tempOk = readTemperatureC(tC);
  if (tempOk) {
    setRelay(tC < TARGET_TEMP_C);
  } else {
    // Safe behavior on invalid temp: heater off
    setRelay(false);
    Serial.println("auto: temp invalid -> heater OFF");
  }

  // 2) Main-jar level control from ECHO pin 25
  float mainJarCm = NAN;
  const bool levelOk = readMainJarLevelCm(mainJarCm);
  if (!levelOk) {
    // Safe behavior on invalid level: both pumps off
    setPumpA(false);
    setPumpB(false);
    Serial.println("auto: level invalid -> both pumps OFF");
    return;
  }

  // Below low bound: fill (pump A ON, pump B OFF)
  if (mainJarCm < MAIN_JAR_LOW_CM) {
    setPumpA(true);
    setPumpB(false);
  }
  // Above high bound: drain (pump B ON, pump A OFF)
  else if (mainJarCm > MAIN_JAR_HIGH_CM) {
    setPumpA(false);
    setPumpB(true);
  }
  // Inside target band: both OFF
  else {
    setPumpA(false);
    setPumpB(false);
  }

  Serial.print("auto: T=");
  if (tempOk) Serial.print(tC, 2); else Serial.print("invalid");
  Serial.print(" C, level(main ECHO25)=");
  Serial.print(mainJarCm, 2);
  Serial.println(" cm");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(OXYGEN_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PUMP_A_PIN, OUTPUT);
  pinMode(PUMP_B_PIN, OUTPUT);

  us.begin();
  us.setTiming(20000UL, 60UL);
  tempSensor.begin();

  setAllOff();

  Serial.println("--- Manual + Auto Bring-up Test ---");
  Serial.println("US: TRIG 14, ECHO 27/26/25 (main jar = ECHO25)");
  Serial.println("Temp: DS18B20 pin 4");
  Serial.println("Auto: heater ON below 27.0 C, OFF at/above 27.0 C");
  Serial.println("Auto: level band 6.0 to 9.0 cm on main jar");
  Serial.println("      below band -> pump A ON, above band -> pump B ON");
  printHelp();
  printStates();
}

void loop() {
  if (autoMode) {
    runAutoControl();
  }

  if (Serial.available() <= 0) {
    delay(10);
    return;
  }

  char c = (char)Serial.read();

  if (c == 'h' || c == 'H') {
    printHelp();
  } else if (c == 's' || c == 'S') {
    printStates();
  } else if (c == 'u' || c == 'U') {
    printUltrasonicOnce();
  } else if (c == 't' || c == 'T') {
    printTemperatureOnce();
  } else if (c == 'm' || c == 'M') {
    autoMode = !autoMode;
    Serial.print("mode -> ");
    Serial.println(autoMode ? "AUTO" : "MANUAL");
    if (!autoMode) {
      Serial.println("manual mode: automation paused");
    }
    printStates();
  } else if (c == 'o' || c == 'O') {
    setOxygen(!oxygenOn);
    printStates();
  } else if (c == 'r' || c == 'R') {
    if (!autoMode) {
      setRelay(!relayOn);
    } else {
      Serial.println("relay manual toggle blocked in AUTO mode");
    }
    printStates();
  } else if (c == 'a' || c == 'A') {
    if (!autoMode) {
      setPumpA(!pumpAOn);
      if (pumpAOn) setPumpB(false);
    } else {
      Serial.println("pump A manual toggle blocked in AUTO mode");
    }
    printStates();
  } else if (c == 'b' || c == 'B') {
    if (!autoMode) {
      setPumpB(!pumpBOn);
      if (pumpBOn) setPumpA(false);
    } else {
      Serial.println("pump B manual toggle blocked in AUTO mode");
    }
    printStates();
  } else if (c == 'x' || c == 'X') {
    setAllOff();
    printStates();
  }
}
