# ECE Senior Design Lab Notebook
## Fermentation Monitoring System
**Controller:** ESP32 | **Course:** ECE Senior Design | Rudy

---

## Entry 1 — January 28, 2026

### Objectives
Establish system concept, identify core hardware, and define short-term demo goals.

### Record

**System Concept:**
The project is a fermentation monitoring and control system. The primary microcontroller selected is the ESP32.

**Sensors identified:**
- pH sensor (probe-based)
- Temperature sensor
- RGB / color sensor
- Ultrasonic sensor (for liquid volume measurement)
- Pressure sensor

**Actuators:** To be determined in subsequent sessions.

**Short-Term Demo Goal:**
Demonstrate a water-to-tea transition. Key observable metrics for the demo are a spike in pH and/or a measurable color change. Data collected during the demo will focus on pH and RGB readings.

---

## Entry 2 — February 8, 2026

### Objectives
Decompose the system into functional subsystems. Confirm hardware components received. Plan demo verification criteria.

### Record

The system is divided into four functional subsystems:

**System 1 — Fermentation Monitoring (F1)**
Monitors temperature, pH, color, and trends. Hardware confirmed received: ESP32 development board. Sensor assignments:
- Temperature: DS18B20 (digital, 1-Wire protocol)
- pH: Probe-based analog sensor
- Color: RGB color sensor
- Liquid level: Ultrasonic sensor

Actuators:
- 12 V heating pad, switched via MOSFET
- 12 V DC pumps with tubing

**System 2 — Fermentation State and Safety**
- Relay to disable heater under fault conditions
- LEDs to indicate normal / abnormal states
- Pressure monitoring

**System 3 — Data Logging**
- ESP32 handles data acquisition and transmission
- Live plots of sensor data displayed to user
- Transmits: temperature, pH, color, pressure

**System 4 — Power**
- 12 V supply to pumps and heater
- 3.3 V supply to sensors and ESP32
- DC-DC buck converter: 12 V → 3.3 V

**Demo Verification Targets:**
- Maintain fermentation temperature within ±1 °C
- pH measurement accuracy (exact specification TBD)
- Detect color changes via RGB sensor
- Adjust liquid composition via pumps
- Display all sensor data in real time
- Raise and stabilize temperature on command

---

## Entry 3 — February 10, 2026 (TA Meeting)

### Objectives
Coordinate with TA, establish meeting cadence, review project deliverables status.

### Record

**Administrative:**
- Weekly TA meeting time set: **Wednesdays, 3:30–4:00 PM**
- Discord group channel created for the project
- Machine shop contacted regarding fabrication of jar stand

**Design Status:**
- Block diagram: complete
- Round 1 PCB: in progress
- Faculty advisor: Professor Zhao

**Action Items Identified:**
- Finalize component procurement list ("everything we can buy" list)
- Begin PCB design for power supply and relay subsystem
- Investigate Peltier controller / heat pump option as alternative to resistive heater
- Initiate PCBway order audit process

---

## Entry 4 — February 17, 2026

### Objectives
Resolve open hardware questions. Define PCB design approach. Source all components.

### Record

**Open Questions Addressed:**

| Question | Resolution / Note |
|---|---|
| ESP32 PCB antenna vs. external RF tube | Flagged for review — ESP32-WROOM-32UE uses external antenna |
| AC → DC 12 V supply (3 rails) | Found commercial unit combining AC/DC and 12 V → 5 V; avoids dangerous in-house AC design |
| DC-DC buck: 12 V → 5 V → 3.3 V | Two-stage conversion confirmed |
| Ultrasonic sensor voltage | HC-SR04 variant operates at 5 V; uses 4 GPIO pins |

**PCB Design Notes:**
- Sensor PCB requires pull-up resistors (e.g., DS18B20 1-Wire requires 4.7 kΩ pull-up to V_DD)
- pH sensor: 5 V → 3.3 V level shift required
- RGB sensor: 5 V supply
- Motor controller IC selected: L298N (dual H-bridge, 2 A max per channel)
- MOSFET driver required for Peltier element
- **Flyback diodes** must be placed across relay coils and L298N motor terminals to suppress inductive voltage spikes when switching inductive loads

**Voltage Regulation Chain:**
AC Mains → [Commercial AC/DC] → 12 V bus → [Buck Converter] → 5 V → [LDO] → 3.3 V
│
Pumps, Heater (12 V)

**Webpage Interaction (planned):**
User interface will allow remote commands including:
- Add sugar
- Add tea
- Set target parameters

**Three ultrasonic sensors planned** (one per container).

---

## Entry 5 — February 20, 2026

### Objectives
Finalize voltage regulation component selection. Define decoupling strategy. Confirm ESP32 module variant.

### Record

**Voltage Regulation:**
- 5 V → 3.3 V: LDO selected — **AMS1117-3.3V** (Low Dropout Regulator)
- ESP32 module confirmed: **ESP32-WROOM-32UE** (external antenna variant)

**Decoupling Capacitor Strategy:**
Decoupling capacitors placed at each IC power pin to suppress high-frequency noise and stabilize rail voltage. Ceramic (non-polarized) capacitors used:

| Location | Value | Notes |
|---|---|---|
| V_IN (5 V rail) | 10 µF + 0.1 µF | Bulk + bypass |
| V_OUT (3.3 V rail) | 10 µF + 0.1 µF | Bulk + bypass |
| Near each IC V_DD pin | 0.1 µF | High-frequency bypass |

> **Equation for LDO Output Voltage** (AMS1117 adjustable variant, if used):
> 
> V_OUT = V_REF × (1 + R2/R1) + I_ADJ × R2
> 
> For fixed 3.3 V variant, output is internally regulated; external resistors not required.

**Additional Components Confirmed:**
- USB connector for ESP32 programming interface
- L298N motor driver: 2 A per channel, used for pump control
- Flyback diodes across relay and L298N outputs (inductive load protection)

---

## Entry 6 — March 4, 2026

### Objectives
Refine control logic. Define sensor validation strategy. Confirm demo specification numbers.

### Record

**Control Logic Design:**
A conditional threshold approach will be used to avoid erroneous actuator triggering. Example: a 0.1 pH spike alone does not immediately activate a pump. A secondary confirmation (e.g., volume check via ultrasonic) is used to validate the reading before acting.

**Sensor Validation Strategy:**
- Cross-reference pH sensor with litmus strip visual check
- Cross-reference temperature sensor reading against secondary method
- Double-check sensor values before any actuator action

**Outstanding Action Item:**
Exact demo specification numbers for pH threshold and target temperature must be confirmed with TA/professor before final integration.

---

## Entry 7 — March 24, 2026

### Objectives
Complete SMD component order. Resolve substitutions for unavailable parts. Confirm all quantities for PCB population.

### Record

**SMD Component Order (quantity multiplied by 3 for spare boards):**

| Component | Package | Qty Ordered | Notes |
|---|---|---|---|
| 0.1 µF capacitor | 0805 | 6 | Standard bypass |
| 10 µF capacitor | 0805 | 6 | Bulk bypass |
| 22 µF capacitor | 0805 | 3 | |
| 1 kΩ resistor | 0805 | 8 | |
| 10 kΩ resistor | 0805 | 4 | |
| 2.2 kΩ resistor | 0805 | 8 | Substituted: 2 kΩ unavailable |
| 4.7 kΩ resistor | 0603 | 6 | Substituted: 0805 4.7 kΩ unavailable, changed package |

**Connector / Through-Hole Orders (Digikey):**

| Part | Qty |
|---|---|
| SMD button switch | 3 |
| Screw terminal (1×1) | 3 |
| 1×19 pin header | 6 |
| 1×4 pin header | 12 |
| 1×3 pin header | 6 |
| 1×2 pin header | 6 |
| 2×2 pin header | 3 |

**IC / Power Component Orders:**

| Component | Part Number | Notes |
|---|---|---|
| LDO 3.3 V | AZ1117CD | Substituted AMS1117 (unavailable) |
| Buck regulator (max 40 V in, 5 V/3 A out) | MAX636ACPA | SMD, Digikey |
| Inductor | 47 µH, 3 A, 11.6 × 10.11 mm | Digikey |
| Schottky diode | IN5922 (3 A) | Substituted IN5824 (5 A unavail.); Digikey |
| 470 µF capacitor | — | 3× from parts drawer |
| 330 µF capacitor | — | 3× from parts drawer |

**Critical Note:** Cannot use ESP32 development kit. Must solder bare ESP32 IC directly to PCB and program via UART/USB interface.

**PCB Error Discovered:** ESP32 left-side pins are mirrored/inverted on the current PCB layout. Must be corrected in next revision.

---

## Entry 8 — April 6, 2026

### Objectives
Continue PCB assembly (Round 3 → Round 4). Debug power rail faults. Correct pin mapping errors.

### Record

**Assembly Status:**
- Soldering of Round 3 PCB completed (board was mistakenly identified as Round 4 throughout this session — confirmed as Round 3 at session end)
- Round 4 PCB rework planned upon resolution of the following faults

**Faults Identified:**

| Fault | Description |
|---|---|
| Wrong component populated | 4.7 kΩ resistors were ordered but 4.7 µF capacitors arrived (quantity/unit error on order form). New order sent for 0.1 µF and correct 4.7 kΩ. |
| No 3.3 V rail | Output reading 1.5–1.6 V instead of 3.3 V |
| ESP32 5 V pin not reading 5 V | Supply not reaching ESP32 V_IN |
| J2 connector mirrored | Left-side ESP32 connector (J2) footprint is flipped on PCB |
| Sensor ground pins not connected | GND traces to sensors missing |
| VDR wrong for echo, pH, and temp pins | Voltage divider ratio incorrect — echo (ultrasonic) and analog sensor pins receiving incorrect voltage |
| Ultrasonic sensor | Echo signal line is 5 V output; ESP32 GPIO is 3.3 V max — requires voltage divider or level shifter |

**GPIO Pin Assignments:**

| Signal | GPIO Pin |
|---|---|
| pH data out | G35 |
| Temperature data | G25 |
| Motor driver IN1/IN2 | G12, G13 |
| Ultrasonic TRIG | G16 |
| Ultrasonic ECHO 1 | G17 |
| Ultrasonic ECHO 2 | G18 |
| Ultrasonic ECHO 3 | G19 |
| Relay 1 | G26 |
| Relay 2 | G27 |


## Entry 9 — April 13, 2026

### Objectives
Implement hardware corrections identified in Entry 8. Finalize PCB fix list before final assembly.

### Record

**Corrections to Implement:**

1. **I²C bus (SCA/SCL):** Connect directly to G22 (SCL) and G21 (SDA). No external pull-up resistors needed on this revision (internal pull-ups enabled in firmware).
2. **pH and echo VDR:** Update voltage divider resistor values per TA (John's) recommendation to correctly level-shift 5 V signals to 3.3 V for ESP32 GPIO input.

   > **Equation 9-1 — Voltage Divider for Level Shifting:**
   > 
   > V_OUT = V_IN × R2 / (R1 + R2)
   > 
   > Target: V_OUT = 3.3 V from V_IN = 5 V
   > 
   > Example: R1 = 2 kΩ, R2 = 3.3 kΩ → V_OUT = 5 × 3.3/5.3 ≈ 3.11 V ✓

3. **Temperature sensor (DS18B20):** V_IN → 3.3 V; data line → 4.7 kΩ pull-up to 3.3 V (per 1-Wire spec).
4. **LDO capacitor layout:** Wrong footprint used for output capacitor — change footprint to match physical component.
5. **Ground plane:** Connect all ground nets; missing GND connections to sensors identified as root cause of "no 3.3 V" fault.
6. **12 V screw terminals:** Add 3 additional screw terminal outputs to distribute 12 V to pumps and heater independently.

---

## Entry 10 — April 22, 2026

### Objectives
Final integration and system-level testing before demo. Confirm all subsystems operational.

### Record

**Pre-Demo Integration Status:**

| Task | Status |
|---|---|
| RGB sensor testing (color detection) | ✅ Complete |
| Oxygen relay on timer | ❌ Not complete |
| 3 ultrasonic distance measurement spikes | ❌ Not complete |
| 3 temperature spike events | ❌ Not complete |
| Wiring cleanup | ✅ Complete |
| Tubing secured | ✅ Complete |
| Heating pad taped down | ✅ Complete |
| PCB mounted on side of enclosure | ✅ Complete |

**Demo Materials Required (bring to demo 04/27):**
- Hot tea
- Vinegar
- Distilled water (hot)
- Kool-Aid

**Demo Sequence Plan (04/27):**
1. Temperature sensor: show live change
2. Ultrasonic sensor: show liquid level change
3. Pumps: demonstrate activation
4. Oxygen relay: demonstrate activation
5. Website display: show live sensor data
6. pH sensor: show reading change
7. RGB sensor: show color change detection

**Alert Conditions Implemented in Firmware:**
- Feed container empty
- Waste container overflow
- Temperature sensor disconnected
- pH sensor wrong value or disconnected
- RGB sensor disconnected
- Temperature sensor fault (general)

**Printed Materials for Demo Reviewers (5 reviewers):**
- Block diagram
- High-level requirements
- Requirements and verifications table

---

*All hand-written entries made in pen.
