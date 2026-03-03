#ifndef TEMP_SENSOR_DS18B20_H
#define TEMP_SENSOR_DS18B20_H

#include <Arduino.h>            
#include <OneWire.h>           
#include <DallasTemperature.h>  

class TempSensorDS18B20 {
public:
    // Constructor
    // Creates a temperature sensor object and stores the GPIO pin
    // Example: TempSensorDS18B20 tempSensor(25);
    explicit TempSensorDS18B20(uint8_t pin);

    // begin()
    // Call once in setup() to initialize the DallasTemperature library
    void begin();

    // request()
    // Tells the sensor to measure temperature now
    // Use this if you want to separate "start measurement" and "read result"
    void request();

    // readC()
    // Returns temperature in Celsius from sensor index 0
    // Assumes request() was called recently (or you are okay with current stored reading)
    float readC();

    // readF()
    // Returns temperature in Fahrenheit from sensor index 0
    // Assumes request() was called recently
    float readF();

    // readCBlocking()
    // Convenience function:
    // 1) requests a new temperature conversion
    // 2) reads and returns Celsius value
    // Good for simple loop logic when you just want a fresh number
    float readCBlocking();

    // readFBlocking()
    // Convenience function:
    // 1) requests a new temperature conversion
    // 2) reads and returns Fahrenheit value
    float readFBlocking();

    // isValid(tempC)
    // Checks whether a Celsius reading is valid
    // Filters out disconnected sensor code (-127 C) and impossible values
    // Use this before making control decisions (heater/pump/fan)
    bool isValid(float tempC) const;

    // isConnected(forceRefresh)
    // Quick check to see if the sensor appears connected and returning valid data.
    // Uses a short cache interval to avoid expensive repeated conversion requests.
    bool isConnected(bool forceRefresh = false);

private:
    // _pin
    // Stores the GPIO pin used for the OneWire data line (DQ)
    uint8_t _pin;

    // _oneWire
    // OneWire bus object used to communicate with the sensor on the given pin
    OneWire _oneWire;

    // _sensors
    // DallasTemperature wrapper object that uses the OneWire bus
    // Provides easy temperature request/read functions
    DallasTemperature _sensors;

    bool _cachedConnected = false;
    unsigned long _lastConnectCheckMs = 0UL;
    static constexpr unsigned long CONNECT_CHECK_INTERVAL_MS = 1000UL;
};

#endif
