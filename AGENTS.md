# AGENTS.md

## Project overview

This repository is for an ECE senior design project: a kombucha fermentation control system.

The coding goal is to support:
- ESP32-based sensing and control
- 12 V DC power architecture
- web dashboard / logging
- safe, simple embedded control logic

Do not redesign the project unless explicitly asked. Work within the assumptions below.

---

## Hardware assumptions

Assume the current system uses:

- ESP32 as the main controller
- 12 V DC barrel power supply
- 12 V peristaltic pumps
- 12 V oxygen pumps
- 12 V heating mats
- DS18B20 temperature sensor
- TCS34725 color sensor
- ultrasonic level sensors
- analog pH sensing board with conditioned output to ESP32 ADC

Power architecture:
- 12 V input is the main system supply
- a 12 V to 5 V converter generates the 5 V rail
- an LDO generates the 3.3 V rail
- 12 V powers the pumps and heating mats
- 5 V powers intermediate logic / modules where needed
- 3.3 V powers the ESP32 logic rail and low-voltage sensor interfaces

Current control grouping:
- 2 peristaltic pumps controlled independently
- 2 oxygen pumps controlled together
- 2 heating mats controlled together

Assume:
- color sensor reads from the bottom of the jar
- heating mats are mounted on the side of the jar
- DS18B20 requires a 4.7 kΩ pull-up to 3.3 V
- ultrasonic sensors may share TRIG and use separate ECHO lines
- ESP32 input pins must never receive more than 3.3 V

---

## Coding priorities

When writing code, prioritize:

1. Clarity
2. Safety
3. Simplicity
4. Debuggability
5. Maintainability

Prefer code that is easy for students to test and modify.

Do not over-engineer unless explicitly requested.

---

## Embedded firmware expectations

For ESP32 firmware:

- Keep modules separated by responsibility
- Prefer readable constants over magic numbers
- Add brief comments for hardware-related logic
- Use safe default states at boot
- Assume actuators should default OFF on startup/reset
- Make control logic explicit and easy to trace
- Prefer simple state-based logic over overly complex abstractions

Suggested module boundaries:
- sensing
- control logic
- actuator control
- communications / dashboard interface
- configuration / thresholds
- logging / status messages

---

## Sensor handling rules

When writing sensing code:

- Validate readings before using them for control decisions
- Handle missing or invalid sensor data safely
- Avoid direct actuator triggering from noisy single samples
- Use averaging, filtering, hysteresis, or threshold bands where appropriate
- Treat pH primarily as a fermentation-progress indicator unless explicitly told otherwise

Temperature:
- used for closed-loop control

Liquid level:
- used for alerts and pump decisions

Color / HLS:
- used for trend monitoring and refresh decisions

pH:
- used mainly for progression / monitoring, not fast closed-loop control

---

## Actuator control rules

When writing actuator code:

- Peristaltic pumps must be independently addressable
- Oxygen pumps should be controlled together as one logical channel
- Heating mats should be controlled together as one logical channel
- Avoid unsafe or conflicting actuator states
- Prefer explicit helper functions like `set_heater(bool on)` over scattered pin writes

If timing or scheduling is needed:
- keep it simple
- make timing constants easy to edit
- log actuator state changes when possible

---

## Dashboard / web interface expectations

For dashboard-related code:

- Show major sensor values clearly
- Show actuator states clearly
- Support timestamped logs
- Support user-editable operating parameters if requested
- Prefer practical status messages over flashy UI features

Useful status logs include:
- heater turned on/off
- oxygen pump turned on/off
- peristaltic pump in/out turned on/off
- alert conditions
- sensor read failures
- system boot / reconnect messages

---

## What not to do unless asked

Do not:
- silently change hardware assumptions
- replace the ESP32 with another platform
- introduce unnecessary frameworks
- add large dependencies without reason
- redesign the project around AC mains
- convert everything into an overly abstract architecture
- remove debug visibility from the code

If an assumption seems wrong, flag it clearly instead of silently changing it.

---

## Preferred output style

When generating code:
- produce complete, usable code
- include brief comments where hardware behavior may be unclear
- preserve existing naming/style if a codebase already exists
- keep functions reasonably short
- make pin mappings and thresholds easy to edit

When suggesting changes:
- explain the reason briefly
- keep recommendations practical
- prefer small, testable changes

---

## If the repo is incomplete

If files or architecture are missing:
- propose a minimal structure
- explain assumptions clearly
- avoid inventing unnecessary subsystems
