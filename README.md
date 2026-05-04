# ORAC-QNode

**Quantum Node Guardian — Real-Time Physical Layer Protection for Quantum Hardware**

> W(t) = Q·D − chi(wear)·T_norm − E_norm·0.22 + phase·0.098 − kappa·U(t)

ORAC-QNode is a deterministic real-time vitality controller that protects the **physical layer** around quantum processors — thermal stability, power delivery aging, and component wear — without machine learning, without lookup tables, in **535 nanoseconds**.

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19019599.svg)](https://doi.org/10.5281/zenodo.19019599)
[![License: Commercial](https://img.shields.io/badge/License-Commercial-red.svg)](LICENSE)
[![Platform: STM32F4](https://img.shields.io/badge/Platform-STM32F4-blue.svg)]()
[![Latency: 535ns](https://img.shields.io/badge/Latency-535ns-green.svg)]()
[![Version: v8](https://img.shields.io/badge/Version-v8-purple.svg)]()

-Validated on ET-MDC1 data. See DOI: 10.5281/zenodo.19959346--

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

text

The system recognizes familiar RF noise as "known" and does not over-react. It activates only under genuine physical threat.

---

## 🆕 What's New in v8

| Feature | Description | Benefit |
|---------|-------------|---------|
| **Phase-Locked Loop (PLL)** | Adaptive phase step with KP=0.15, KI=0.02 | Synchronizes with external quantum signals |
| **Coherence Metric** | Real-time phase alignment measurement (0-1) | Quantifies resonance quality |
| **Resonant State Detection** | New `RESONANT` status when W>0.45 & COH>0.78 | Optimal performance indicator |
| **Fast Thermal Recovery** | Returns to HEALTHY within 2.5s after 120°C spike | Production-ready resilience |
| **Sensor-Agnostic Validation** | Same pipeline for 3 different sensors | Unprecedented versatility |

**Performance validation (v8):**

| Metric | Result |
|--------|--------|
| Sensor-agnostic correlation | W1 ≈ W2 ≈ W3 (error < 0.002) |
| Recovery time after 120°C spike | < 2.5 seconds |
| Coherence range | 0.52 — 1.00 |
| Resonant detection | 36+ times in 100-second test |

---

## Hardware Validation

Validated on **STM32F401CCU6 @ 84 MHz** with DWT cycle counter (1-cycle resolution):

| Implementation | Min cycles | Avg cycles | Min latency | Avg latency |
|---|---|---|---|---|
| v4 software math | 351 | 593 | 4178 ns | 7059 ns |
| v4 FPU float | 201 | 204 | 2393 ns | 2429 ns |
| **v7e Q15/Q12** | **45** | **45–55** | **535 ns** | **535–654 ns** |
| **v8 (same core)** | **45** | **45–55** | **535 ns** | **535–654 ns** |

Two deterministic latency tiers:
- **535 ns** (45 cycles) — T outside wear-active zone
- **654 ns** (55 cycles) — Arrhenius path active (55–83°C)

Zero variance within each tier.

**RAM footprint:** 14 bytes state (v7) / 24 bytes state (v8 with PLL)  
**Flash:** ~320 bytes (v7) / ~450 bytes (v8)  
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

We tested the hypothesis that the **same** `orac_single_node_v8.h` pipeline can maintain stability (W > 0) across three fundamentally different sensors exposed to identical physical conditions:

| Sensor | Type | Cost | Response |
|--------|------|------|----------|
| DS18B20 | Temperature (digital) | $2 | Slow thermal mass |
| MPU6050 | Accelerometer + Gyro | $10 | Fast vibration response |
| NV-center (emulated) | Quantum sensor | $0 (sim) | 1/f noise + fast decoherence |

**The Experiment:** Thermal cycle (25°C → 65°C → 25°C) with an **extreme 120°C spike** between 30-35 seconds. The hypothesis would be **falsified** if the three `W` values showed differing signs at any time step.

**The Result:** ✅ **All three sensors maintained identical sign throughout the entire experiment**, including during the 120°C spike. The hypothesis was **not falsified**, supporting sensor-agnostic operation.

**Recovery Performance:** After the spike, the system returned to HEALTHY status within **2.5 seconds**, demonstrating production-ready resilience.

**Try it yourself (no hardware required):**
```bash
gcc -lm -o orac_demo orac_minimal_demo_v8.c && ./orac_demo
See orac_minimal_demo_v8.c for the complete test.

🎯 Resonance Detection
The v8 system can detect and report resonant states — optimal operating conditions where phase coherence and vitality are maximized:

text
RESONANT status conditions:
- W ≥ 0.45
- Coherence ≥ 0.78
- Phase error minimized via PLL
When to use RESONANT detection:

Quantum sensing (NV-center, atomic interferometers): Maximum sensitivity requires resonant operation

Navigation (GPS-denied environments): Maintaining lock is mission-critical

Medical diagnostics (QT Sense, NVision): Diagnostic accuracy depends on resonance quality

Two Deployment Variants
Variant A — Single Node Guardian
For one SpinQ Gemini or compatible NMR quantum computer.

text
Hardware: STM32F4 + DS18B20 temperature sensor
Interface: USB Serial → host computer
Output:   RESONANT / HEALTHY / WARM / THROTTLE / CRITICAL / DEAD
Latency:  535 ns
Memory:   24 bytes state
Files:    orac_single_node_v8.h + orac_single_node_v8.ino
Variant B — Network Guardian
For a network of 2–8 quantum nodes. Adds adaptive coupling between nodes, load balancing, and predictive lambda activation.

text
Hardware: STM32F4 + I2C multiplexer + N sensors
Adaptive coupling: lambda 0.10–0.40 (predictive + hysteresis)
Load balancing: automatic redistribution
Network output: NET:OK/WARN/ALERT/EMERGENCY + per-node status
Files:    orac_network_v8.h + orac_network_v8.ino
File Structure
text
ORAC-QNode/
├── README.md
├── LICENSE
├── orac_single_node_v7e.h       # Core v7 (stable, 14 bytes RAM)
├── orac_single_node_v8.h         # 🆕 v8 with PLL + resonance (24 bytes RAM)
├── orac_single_node_v7e.ino      # Single node main loop
├── orac_network_v7e.h            # Network guardian
├── orac_network_v7e.ino          # Network main loop
├── orac_spinqit_wrapper.py       # Python SpinQit integration
├── orac_minimal_demo.c           # v7 demo (3 sensors, 120°C spike)
├── orac_minimal_demo_v8.c        # 🆕 v8 demo with PLL + resonance
└── hardware/                     # Legacy hardware files
    ├── orac_nt_vitality_v7e.h
    ├── orac_stm32_v7e.ino
    └── ...
Quick Start
Single node (STM32) with v7 (stable):

cpp
#include "orac_single_node_v7e.h"

ORAC_SingleState S;
orac_single_init(&S);

// In loop:
float T    = read_temperature();
float load = compute_load(T);
ORAC_Result r = orac_single_step(&S, T, load);
// Output: STEP|T|W|U|E|STATUS
Single node with v8 (PLL + resonance):

cpp
#include "orac_single_node_v8.h"

ORAC_SingleState S;
orac_single_init(&S);

// In loop (note: time_sec required for PLL):
float T    = read_temperature();
float load = compute_load(T);
float t    = millis() / 1000.0f;
ORAC_Result r = orac_single_step(&S, T, load, t);
// Output includes coherence: STEP|T|W|U|E|COH|STATUS
Sensor-Agnostic Demo with v8:

bash
# Compile and run — proves same W works for 3 different sensors
gcc -lm -o orac_demo_v8 orac_minimal_demo_v8.c
./orac_demo_v8
Python SpinQit wrapper:

bash
# Demo mode (no hardware)
python orac_spinqit_wrapper.py --demo

# With STM32 on COM3
python orac_spinqit_wrapper.py --port COM3 --mode single

# Network mode (4 nodes)
python orac_spinqit_wrapper.py --port COM3 --mode network
Serial Output Format
Single node (v7):

text
42|67.3|0.4120|0.0870|0.5310|WARM
STEP | T(°C) | W | U_t | E_norm | STATUS

Single node (v8):

text
42|67.3|0.4120|0.0870|0.5310|0.8472|RESONANT
STEP | T(°C) | W | U_t | E_norm | COH | STATUS

Network:

text
NET:OK|W:0.712|U:0.091|L:0.23|FAIL:0/4
N0:HEALTHY|N1:WARM|N2:HEALTHY|N3:THROTTLE
Status Definitions
Status	W range	Coherence (v8)	Description
RESONANT (v8 only)	≥ 0.45	≥ 0.78	Optimal operation, phase-locked
HEALTHY	0.30 – 1.00	≥ 0.60	Normal operation
WARM	0.00 – 0.29	0.45 – 0.60	Elevated temperature, reduce load
THROTTLE	-0.119 – 0.00	0.30 – 0.45	Active cooling required
CRITICAL	-0.699 – -0.120	< 0.30	Imminent damage, emergency
DEAD	< -0.70	< 0.30	Sacrificed, hardware protection active
Scientific Reference
DOI: 10.5281/zenodo.19019599

SDK: github.com/Kretski/orac-nt-ssd-thermal-sdk

Author: Dimitar Kretski, Independent Researcher, Varna, Bulgaria

License
This software is proprietary. See LICENSE for full terms.

Commercial use requires a license.
For licensing inquiries: kretski1@gmail.com

Academic evaluation licenses available — contact for details.
