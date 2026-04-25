# ORAC-QNode

**Quantum Node Guardian — Real-Time Physical Layer Protection for Quantum Hardware**

> W(t) = Q·D − chi(wear)·T_norm − E_norm·0.18 + omega·phase·0.098

ORAC-QNode is a deterministic real-time vitality controller that protects the **physical layer** around quantum processors — thermal stability, power delivery aging, and component wear — without machine learning, without lookup tables, in **535 nanoseconds**.

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19019599.svg)](https://doi.org/10.5281/zenodo.19019599)
[![License: Commercial](https://img.shields.io/badge/License-Commercial-red.svg)](LICENSE)
[![Platform: STM32F4](https://img.shields.io/badge/Platform-STM32F4-blue.svg)]()
[![Latency: 535ns](https://img.shields.io/badge/Latency-535ns-green.svg)]()

---

## What It Does

Quantum hardware is sensitive to three physical disturbances:

- **Temperature fluctuations** — cryogenic drift, thermal spikes
- **Component aging** — elevated BER as hardware accumulates wear
- **Power delivery noise** — voltage transients reaching the quantum layer

ORAC-QNode addresses all three at the physical layer — in the control electronics surrounding the quantum processor, not at the qubit level. It acts as a predictive buffer that prevents thermal and electrical shocks from reaching the quantum layer.

**Scope:** This system manages the physical environment around the quantum processor. It does NOT control qubits directly. It signals the host computer to adjust job scheduling based on real-time physical health.

---

## Key Innovation: Uncertainty-Aware Vitality

Standard thermal guardians react only to instantaneous temperature. ORAC-QNode adds two new metrics:

**sigma_E** — standard deviation of thermal energy over the last 30 steps. Measures how chaotic the system is behaving.

**f_familiarity** — how much the current behavior deviates from historical baseline. If the system is in an unfamiliar regime, uncertainty rises.
U(t) = sigma_E + alpha * (1 - f_familiarity)

W(t) = Q_perf - T_norm*0.84 - E_norm*0.22 + phase*0.098 - kappa*U(t)

The system recognizes familiar RF noise as "known" and does not over-react. It activates only under genuine physical threat.

---

## Hardware Validation

Validated on **STM32F401CCU6 @ 84 MHz** with DWT cycle counter (1-cycle resolution):

| Implementation | Min cycles | Avg cycles | Min latency | Avg latency |
|---|---|---|---|---|
| v4 software math | 351 | 593 | 4178 ns | 7059 ns |
| v4 FPU float | 201 | 204 | 2393 ns | 2429 ns |
| **v7e Q15/Q12** | **45** | **45–55** | **535 ns** | **535–654 ns** |

Two deterministic latency tiers:
- **535 ns** (45 cycles) — T outside wear-active zone
- **654 ns** (55 cycles) — Arrhenius path active (55–83°C)

Zero variance within each tier.

**RAM footprint:** 14 bytes state  
**Flash:** ~320 bytes  
**Dependencies:** None (single header, C99)

---

## Simulation Results

Arrhenius-weighted simulation, 2000 steps, 90% workload vs fixed-threshold control:

| Metric | Fixed threshold | ORAC-QNode | Delta |
|---|---|---|---|
| Avg junction temp | 68.5°C | 63.0°C | **−5.6°C** |
| NAND quality Q | 0.631 | 0.669 | **+6.0%** |
| Component lifetime | baseline | +31.6% | **+31.6%** |
| Wear accumulation | baseline | −27.8% | **−27.8%** |

---

## 🧪 Experimental Validation: Sensor-Agnostic Proof

We tested the hypothesis that the **same** `orac_single_node_v7e.h` pipeline can maintain stability (W > 0) across three fundamentally different sensors exposed to identical physical conditions:

| Sensor | Type | Cost | Response |
|--------|------|------|----------|
| DS18B20 | Temperature (digital) | $2 | Slow thermal mass |
| MPU6050 | Accelerometer + Gyro | $10 | Fast vibration response |
| NV-center (emulated) | Quantum sensor | $0 (sim) | 1/f noise + fast decoherence |

**The Experiment:** Thermal cycle (25°C → 65°C → 25°C) with an **extreme 120°C spike** between 30-35 seconds. The hypothesis would be **falsified** if the three `W` values showed differing signs at any time step.

**The Result:** ✅ **All three sensors maintained identical sign throughout the entire experiment**, including during the 120°C spike. The hypothesis was **not falsified**, supporting sensor-agnostic operation.

**Try it yourself (no hardware required):**
```bash
gcc -lm -o orac_demo orac_minimal_demo.c && ./orac_demo

The system recognizes familiar RF noise as "known" and does not over-react. It activates only under genuine physical threat.

---

## Hardware Validation

Validated on **STM32F401CCU6 @ 84 MHz** with DWT cycle counter (1-cycle resolution):

| Implementation | Min cycles | Avg cycles | Min latency | Avg latency |
|---|---|---|---|---|
| v4 software math | 351 | 593 | 4178 ns | 7059 ns |
| v4 FPU float | 201 | 204 | 2393 ns | 2429 ns |
| **v7e Q15/Q12** | **45** | **45–55** | **535 ns** | **535–654 ns** |

Two deterministic latency tiers:
- **535 ns** (45 cycles) — T outside wear-active zone
- **654 ns** (55 cycles) — Arrhenius path active (55–83°C)

Zero variance within each tier.

**RAM footprint:** 14 bytes state  
**Flash:** ~320 bytes  
**Dependencies:** None (single header, C99)

---

## Simulation Results

Arrhenius-weighted simulation, 2000 steps, 90% workload vs fixed-threshold control:

| Metric | Fixed threshold | ORAC-QNode | Delta |
|---|---|---|---|
| Avg junction temp | 68.5°C | 63.0°C | **−5.6°C** |
| NAND quality Q | 0.631 | 0.669 | **+6.0%** |
| Component lifetime | baseline | +31.6% | **+31.6%** |
| Wear accumulation | baseline | −27.8% | **−27.8%** |

---

## 🧪 Experimental Validation: Sensor-Agnostic Proof

We tested the hypothesis that the **same** `orac_single_node_v7e.h` pipeline can maintain stability (W > 0) across three fundamentally different sensors exposed to identical physical conditions:

| Sensor | Type | Cost | Response |
|--------|------|------|----------|
| DS18B20 | Temperature (digital) | $2 | Slow thermal mass |
| MPU6050 | Accelerometer + Gyro | $10 | Fast vibration response |
| NV-center (emulated) | Quantum sensor | $0 (sim) | 1/f noise + fast decoherence |

**The Experiment:** Thermal cycle (25°C → 65°C → 25°C) with an **extreme 120°C spike** between 30-35 seconds. The hypothesis would be **falsified** if the three `W` values showed differing signs at any time step.

**The Result:** ✅ **All three sensors maintained identical sign throughout the entire experiment**, including during the 120°C spike. The hypothesis was **not falsified**, supporting sensor-agnostic operation.

**Try it yourself (no hardware required):**
```bash
gcc -lm -o orac_demo orac_minimal_demo.c && ./orac_demo
See orac_minimal_demo.c for the complete test.

Experimental Design for Real Hardware (Technical Annex)
To validate on physical hardware, we propose a controlled setup:

Platform: Aluminum plate + PTC heater + fan (<$100)

Sensors: DS18B20 ($2), MPU6050 ($10), optional real NV-center

ORAC Hardware: STM32F103C8T6 + OLED + SD card ($20)

Total Budget: <$300

Success Criterion: Correlation of W across sensors > 0.95

Falsification Criterion: Any sign disagreement between sensor W values

Full experimental procedure available upon request.

Two Deployment Variants
Variant A — Single Node Guardian
For one SpinQ Gemini or compatible NMR quantum computer.

Hardware: STM32F4 + DS18B20 temperature sensor
Interface: USB Serial → host computer
Output:   HEALTHY / WARM / THROTTLE / CRITICAL
Latency:  535 ns
Files:    orac_single_node_v7e.h + orac_single_node_v7e.ino
Variant B — Network Guardian
For a network of 2–8 quantum nodes. Adds adaptive coupling between nodes, load balancing, and predictive lambda activation.
Hardware: STM32F4 + I2C multiplexer + N sensors
Adaptive coupling: lambda 0.10–0.40 (predictive + hysteresis)
Load balancing: automatic redistribution
Network output: NET:OK/WARN/ALERT/EMERGENCY + per-node status
Files:    orac_network_v7e.h + orac_network_v7e.ino
File Structure
ORAC-QNode/
├── README.md
├── LICENSE
├── orac_single_node_v7e.h       # Core single-node guardian
├── orac_single_node_v7e.ino     # Single node main loop
├── orac_network_v7e.h           # Network guardian
├── orac_network_v7e.ino         # Network main loop
├── orac_spinqit_wrapper.py      # Python SpinQit integration
├── orac_minimal_demo.c          # 🆕 Sensor-agnostic demo (3 sensors, 120°C spike)
└── hardware/                    # Legacy hardware files
    ├── orac_nt_vitality_v7e.h
    ├── orac_stm32_v7e.ino
    └── ...
	Quick Start
Single node (STM32)
#include "orac_single_node_v7e.h"

ORAC_SingleState S;
orac_single_init(&S);

// In loop:
float T    = read_temperature();
float load = compute_load(T);
ORAC_Result r = orac_single_step(&S, T, load);

// Serial output: STEP|T|W|U|E|STATUS
Sensor-Agnostic Demo (new!)
# Compile and run — proves same W works for 3 different sensors
gcc -lm -o orac_demo orac_minimal_demo.c
./orac_demo
Python SpinQit wrapper
# Demo mode (no hardware)
python orac_spinqit_wrapper.py --demo

# With STM32 on COM3
python orac_spinqit_wrapper.py --port COM3 --mode single

# Network mode (4 nodes)
python orac_spinqit_wrapper.py --port COM3 --mode network
Serial Output Format
Single node:

text
42|67.3|0.4120|0.0870|0.5310|WARM
STEP | T(°C) | W | U_t | E_norm | STATUS

Network:
NET:OK|W:0.712|U:0.091|L:0.23|FAIL:0/4
N0:HEALTHY|N1:WARM|N2:HEALTHY|N3:THROTTLE
Scientific Reference
DOI: 10.5281/zenodo.19019599

SDK: github.com/Kretski/orac-nt-ssd-thermal-sdk

Author: Dimitar Kretski, Independent Researcher, Varna, Bulgaria

License
This software is proprietary. See LICENSE for full terms.

Commercial use requires a license.
For licensing inquiries: kretski1@gmail.com

Academic evaluation licenses available — contact for details.
