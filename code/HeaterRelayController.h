#pragma once

#include <Arduino.h>

// HeaterRelayController
// Controls two relay channels that switch two heating mats.
// Relay input side is ESP32 GPIO logic.
// Relay contact side switches 12 V power to each heat mat.
class HeaterRelayController {
public:
  // activeLow = true for common relay modules where LOW means ON.
  HeaterRelayController(int relayPinA, int relayPinB, bool activeLow = true);

  // Initializes GPIO and forces both channels OFF for safe boot.
  void begin();

  // Control each heating mat independently.
  void setMatA(bool on);
  void setMatB(bool on);

  // Logical grouped control: both mats together.
  void setHeater(bool on);

  // Safe shutdown helper.
  void offAll();

  // Optional status helpers.
  bool isMatAOn() const;
  bool isMatBOn() const;

private:
  void writeRelayPin(int pin, bool on) const;

  int _relayPinA;
  int _relayPinB;
  bool _activeLow;
  bool _matAOn;
  bool _matBOn;
};
