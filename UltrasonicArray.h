#pragma once
#include <Arduino.h>

struct USReading {
  float cm = -1.0f;   // -1 means invalid
  bool ok = false;
};

class UltrasonicArray {
public:
  UltrasonicArray(int trigPin, const int* echoPins, int count);

  void begin();
  void setTiming(unsigned long timeoutUs, unsigned long quietMs);

  // Read one sensor (index into echoPins array)
  USReading readOne(int index);

  // Convenience: read all sensors into an output array (size >= count)
  void readAll(USReading* outReadings);

  int count() const { return _count; }

private:
  void triggerPing();

  int _trig;
  const int* _echoPins;
  int _count;

  unsigned long _timeoutUs = 20000UL; // good for jars
  unsigned long _quietMs   = 60UL;    // reduce crosstalk

  static constexpr float SPEED_CM_PER_US = 0.0343f; // speed of sound
};