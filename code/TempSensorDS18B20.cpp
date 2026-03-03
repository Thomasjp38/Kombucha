#include "TempSensorDS18B20.h"

TempSensorDS18B20::TempSensorDS18B20(uint8_t pin)
    : _pin(pin), _oneWire(pin), _sensors(&_oneWire) {}

void TempSensorDS18B20::begin() {
    _sensors.begin();
}

void TempSensorDS18B20::request() {
    _sensors.requestTemperatures();
}

float TempSensorDS18B20::readC() {
    return _sensors.getTempCByIndex(0);
}

float TempSensorDS18B20::readF() {
    return _sensors.getTempFByIndex(0);
}

float TempSensorDS18B20::readCBlocking() {
    request();
    return readC();
}

float TempSensorDS18B20::readFBlocking() {
    request();
    return readF();
}

bool TempSensorDS18B20::isValid(float tempC) const {
    // DallasTemperature uses DEVICE_DISCONNECTED_C = -127
    // Also reject weird impossible values just in case
    return (tempC != DEVICE_DISCONNECTED_C && tempC > -55.0f && tempC < 125.0f);
}

bool TempSensorDS18B20::isConnected(bool forceRefresh) {
    const unsigned long now = millis();
    if (!forceRefresh && (now - _lastConnectCheckMs) < CONNECT_CHECK_INTERVAL_MS) {
        return _cachedConnected;
    }

    request();
    const float tC = readC();
    _cachedConnected = isValid(tC);
    _lastConnectCheckMs = now;
    return _cachedConnected;
}
