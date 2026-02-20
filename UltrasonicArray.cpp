#include "UltrasonicArray.h"

UltrasonicArray::UltrasonicArray(int trigPin, const int* echoPins, int count)
  : _trig(trigPin), _echoPins(echoPins), _count(count) {}

void UltrasonicArray::begin() {
  pinMode(_trig, OUTPUT);
  digitalWrite(_trig, LOW);

  for (int i = 0; i < _count; i++) {
    pinMode(_echoPins[i], INPUT);
  }
}

void UltrasonicArray::setTiming(unsigned long timeoutUs, unsigned long quietMs) {
  _timeoutUs = timeoutUs;
  _quietMs = quietMs;
}

void UltrasonicArray::triggerPing() {
  digitalWrite(_trig, LOW);
  delayMicroseconds(2);
  digitalWrite(_trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(_trig, LOW);
}

USReading UltrasonicArray::readOne(int index) {
  USReading r;
  if (index < 0 || index >= _count) return r;

  // Let previous echoes die down (helps reduce cross-talk)
  delay(_quietMs);

  // Fire all sensors (shared trig), then measure only selected echo pin
  triggerPing();

  unsigned long duration = pulseIn(_echoPins[index], HIGH, _timeoutUs);
  if (duration == 0) return r;  // timeout/no echo

  r.ok = true;
  r.cm = (duration * SPEED_CM_PER_US) / 2.0f;
  return r;
}

void UltrasonicArray::readAll(USReading* outReadings) {
  for (int i = 0; i < _count; i++) {
    outReadings[i] = readOne(i);
  }
}