#pragma once
#include <Arduino.h>

class PumpController {
public:
  PumpController(int enaPin, int enbPin, int pwmFreq = 20000, int pwmResBits = 8, int minDuty = 80);

  bool begin();

  // Basic on/off
  void onA(int duty = -1);
  void onB(int duty = -1);
  void offA();
  void offB();

  // Both pumps
  void onBoth(int duty = -1);
  void offAll();

  // Direct duty control
  void setDutyA(int duty);
  void setDutyB(int duty);
  void setBoth(int duty);

  // Optional getters
  int getDutyA() const;
  int getDutyB() const;
  int getMinDuty() const;

private:
  int clampDuty(int duty) const;

  int _enaPin;
  int _enbPin;
  int _pwmFreq;
  int _pwmResBits;
  int _minDuty;
  int _dutyMax;

  int _currentDutyA;
  int _currentDutyB;
};