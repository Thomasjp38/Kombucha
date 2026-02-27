#include "PumpController.h"

PumpController::PumpController(int enaPin, int enbPin, int pwmFreq, int pwmResBits, int minDuty)
  : _enaPin(enaPin),
    _enbPin(enbPin),
    _pwmFreq(pwmFreq),
    _pwmResBits(pwmResBits),
    _minDuty(minDuty),
    _dutyMax((1 << pwmResBits) - 1),
    _currentDutyA(0),
    _currentDutyB(0) {}

bool PumpController::begin() {
  bool okA = ledcAttach(_enaPin, _pwmFreq, _pwmResBits);
  bool okB = ledcAttach(_enbPin, _pwmFreq, _pwmResBits);

  if (!(okA && okB)) {
    return false;
  }

  offAll();
  return true;
}

int PumpController::clampDuty(int duty) const {
  if (duty < 0) return 0;
  if (duty > _dutyMax) return _dutyMax;
  return duty;
}

void PumpController::setDutyA(int duty) {
  _currentDutyA = clampDuty(duty);
  ledcWrite(_enaPin, _currentDutyA);
}

void PumpController::setDutyB(int duty) {
  _currentDutyB = clampDuty(duty);
  ledcWrite(_enbPin, _currentDutyB);
}

void PumpController::setBoth(int duty) {
  setDutyA(duty);
  setDutyB(duty);
}

void PumpController::onA(int duty) {
  if (duty < 0) duty = _minDuty;
  setDutyA(duty);
}

void PumpController::onB(int duty) {
  if (duty < 0) duty = _minDuty;
  setDutyB(duty);
}

void PumpController::offA() {
  setDutyA(0);
}

void PumpController::offB() {
  setDutyB(0);
}

void PumpController::onBoth(int duty) {
  if (duty < 0) duty = _minDuty;
  setBoth(duty);
}

void PumpController::offAll() {
  setDutyA(0);
  setDutyB(0);
}

int PumpController::getDutyA() const {
  return _currentDutyA;
}

int PumpController::getDutyB() const {
  return _currentDutyB;
}

int PumpController::getMinDuty() const {
  return _minDuty;
}