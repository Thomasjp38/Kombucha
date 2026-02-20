#include <Arduino.h>
#include "UltrasonicArray.h"

constexpr int TRIG_PIN = 4;
constexpr int ECHO_PINS[] = {16, 17}; // each through divider
constexpr int N = sizeof(ECHO_PINS) / sizeof(ECHO_PINS[0]);

UltrasonicArray us(TRIG_PIN, ECHO_PINS, N);

void setup() {
  Serial.begin(115200);
  delay(200);

  us.begin();

  // For jar distances, shorter timeout is fine; quiet time reduces interference.
  us.setTiming(20000, 60);

  Serial.println("Ultrasonic array ready.");
}

void loop() {
  for (int i = 0; i < us.count(); i++) {
    USReading r = us.readOne(i);
    Serial.print("Jar "); Serial.print(i); Serial.print(": ");

    if (r.ok) {
      Serial.print(r.cm, 1);
      Serial.println(" cm");
    } else {
      Serial.println("timeout");
    }
  }
  Serial.println();
  delay(250);
}