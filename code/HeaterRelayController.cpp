#include "HeaterRelayController.h"

HeaterRelayController::HeaterRelayController(int relayPinA, int relayPinB, bool activeLow)
  : _relayPinA(relayPinA),
    _relayPinB(relayPinB),
    _activeLow(activeLow),
    _matAOn(false),
    _matBOn(false) {}

void HeaterRelayController::begin() {
  pinMode(_relayPinA, OUTPUT);
  pinMode(_relayPinB, OUTPUT);
  offAll();
}

void HeaterRelayController::writeRelayPin(int pin, bool on) const {
  // Many 5 V relay modules are active-low on input pins.
  int level = on ? (_activeLow ? LOW : HIGH) : (_activeLow ? HIGH : LOW);
  digitalWrite(pin, level);
}

void HeaterRelayController::setMatA(bool on) {
  _matAOn = on;
  writeRelayPin(_relayPinA, on);
}

void HeaterRelayController::setMatB(bool on) {
  _matBOn = on;
  writeRelayPin(_relayPinB, on);
}

void HeaterRelayController::setHeater(bool on) {
  setMatA(on);
  setMatB(on);
}

void HeaterRelayController::offAll() {
  setHeater(false);
}

bool HeaterRelayController::isMatAOn() const {
  return _matAOn;
}

bool HeaterRelayController::isMatBOn() const {
  return _matBOn;
}
