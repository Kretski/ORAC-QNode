# ORAC-QNode

**Quantum Node Guardian — Real-Time Physical Layer Protection for Quantum Hardware**

> $W(t) = Q \cdot D - \chi(wear) \cdot T_{norm} - E_{norm} \cdot 0.22 + phase \cdot 0.098 - \kappa \cdot U(t)$

ORAC-QNode is a deterministic real-time vitality controller that protects the **physical layer** surrounding quantum processors — thermal stability, power delivery aging, and component wear — without machine learning or lookup tables, achieving a latency of **535 nanoseconds**.

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19019599.svg)](https://doi.org/10.5281/zenodo.19019599)
[![License: Commercial](https://img.shields.io/badge/License-Commercial-red.svg)](LICENSE)
[![Platform: STM32F4](https://img.shields.io/badge/Platform-STM32F4-blue.svg)]()
[![Latency: 535ns](https://img.shields.io/badge/Latency-535ns-green.svg)]()
[![Version: v8](https://img.shields.io/badge/Version-v8-purple.svg)]()

---

[![GCN Schema: v7.0.0](https://img.shields.io/badge/NASA%20GCN%20Schema-v7.0.0-orange.svg)](https://gcn.nasa.gov/docs/schema)

> 🚀 **Universal Interoperability:** Now supporting NASA GCN Schema v7.0.0. ORAC-QNode can intercept external hardware anomalies and astrophysical transient alerts in real-time to force-protect quantum coherence.

---

## What It Does

Quantum hardware is sensitive to physical disturbances that occur at the control layer:
* **Temperature fluctuations** — cryogenic drift and thermal spikes.
* **Component aging** — elevated Bit Error Rates (BER) as hardware accumulates wear.
* **Power delivery noise** — voltage transients affecting the quantum substrate.

**Scope:** This system acts as a predictive buffer for the control electronics. It does NOT control qubits directly but signals the host computer to adjust job scheduling based on real-time physical health.

---

## 🚀 New in Version 8 (v8 Upgrades)

* **Universal External Alert Bridge:** A dedicated, multi-threaded listener for intercepting external M2M (Machine-to-Machine) telemetry signals.
* **NASA GCN Compliance:** Full native support for NASA's General Coordinates Network (GCN) v7.0.0 JSON schema to detect transient events like Gravitational Waves in real time.
* **Deterministic Hardware Lock:** Instant hardware priority override that locks core vitality $W$ at `0.99` (`EMERGENCY_RESONANT`) during active alerts.
* **Phase-Locked Loop (PLL):** Adaptive phase synchronization with external quantum signals.

---

## 🧪 Experimental Validation: Sensor-Agnostic Proof

We verified that the same pipeline maintains stability ($W > 0$) across three fundamentally different sensors:
1.  **DS18B20:** Digital thermometer (slow thermal mass).
2.  **MPU6050:** Accelerometer (fast vibration response).
3.  **NV-center (emulated):** Quantum sensor with 1/f noise.

**Result:** ✅ All sensors maintained identical sign during an extreme **120°C spike**, with the system returning to HEALTHY status in less than **2.5 seconds**.

---

## 📊 Status Matrix & Operational Boundaries

| Status | W Range | Coherence (v8) | Operational Response |
| :--- | :--- | :--- | :--- |
| **RESONANT** | ≥ 0.45 | ≥ 0.78 | Optimal operation; phase-locked via internal PLL. |
| **EMERGENCY** | Fixed 0.99 | Locked | Forced protection mode triggered by NASA/Industrial alerts. |
| **HEALTHY** | 0.30 – 1.00 | ≥ 0.60 | Normal operation; standard task scheduling. |
| **WARM** | 0.00 – 0.29 | 0.45 – 0.60 | Thermal drift detected; throttles SpinQit job shots. |
| **CRITICAL** | < -0.120 | < 0.30 | High damage vector; pauses job execution queues. |

---

## 💻 Technical Structure

* `orac_spinqit_wrapper.py`: Python bridge for integration with **SpinQit** (SpinQ Gemini).
* `orac_minimal_demo_v8.c`: Core C engine for deterministic mathematics and sensor validation.
* `orac_single_node_v8.h`: Ultra-lightweight header (24 bytes RAM) for STM32F4 deployment.

---

## Scientific Reference

**Core Repository:** [github.com/Kretski/ORAC-QNode](https://github.com/Kretski/ORAC-QNode)

**Official Theoretical Foundation DOI:** [10.5281/zenodo.19019599](https://doi.org/10.5281/zenodo.19019599)
*Note: This DOI links to the peer-reviewed paper "ORAC-NT v5.x: Optimal and Stable FDIR Architecture for Autonomous Spacecraft and Critical Systems". The stability metrics established in v5.x have been mathematically extended to derive the vitality $W(t)$ framework for quantum hardware protection.*

**Author:** Dimitar Kretski, Independent Researcher, Varna, Bulgaria

---

## License
This software is proprietary. Commercial use, deployment, or institutional evaluation requires an active license.
For licensing inquiries: **kretski1@gmail.com**

