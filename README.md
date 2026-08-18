# ORAC-QNode

**Quantum Node Guardian — Dual-Layer Protection & Efficiency Stack for Quantum Hardware**

> $W(t) = Q \cdot D - \chi(wear) \cdot T_{norm} - E_{norm} \cdot 0.22 + phase \cdot 0.098 - \kappa \cdot U(t)$

ORAC-QNode is a software-defined dual-layer stack that transforms unstable quantum and peripheral controllers into industrially resilient infrastructure. It combines deterministic bare-metal protection with hardware-informed algorithmic efficiency — achieving **535 ns response latency** and up to **20%+ quantum shot savings** without machine learning or lookup tables.

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19019599.svg)](https://doi.org/10.5281/zenodo.19019599)[![License Commercial](https://img.shields.io/badge/License-Commercial-red.svg)](LICENSE)[![Platform STM32F4](https://img.shields.io/badge/Platform-STM32F4-blue.svg)]()[![Latency 535ns](https://img.shields.io/badge/Latency-535ns-green.svg)]()[![Version v8](https://img.shields.io/badge/Version-v8-purple.svg)]()[![GCN Schema v700](https://img.shields.io/badge/NASA%20GCN%20Schema-v7.0.0-orange.svg)](https://gcn.nasa.gov/docs/schema)

* * *

## Stack Architecture

    ┌─────────────────────────────────────────────────────────────────┐
    │              ORAC-QNode  Dual-Layer Stack                       │
    │                                                                 │
    │  ┌──────────────────────────────────────────────────────────┐  │
    │  │  L2 — GravOptAdaptiveE_QV  (Algorithmic Efficiency)      │  │
    │  │  VQE / QAOA parameter gatekeeper                         │  │
    │  │  W(t) + E_norm → adaptive shot budget + freeze mask      │  │
    │  └──────────────────────┬───────────────────────────────────┘  │
    │                         │  telemetry (W, E_norm, status)        │
    │  ┌──────────────────────▼───────────────────────────────────┐  │
    │  │  L1 — ORAC-NT / ORAC-QNode  (Bare-Metal Hardware Shield) │  │
    │  │  535 ns deterministic response · <1.2 ms SEU recovery    │  │
    │  │  STM32F4 · ARM / RISC-V · Zero ML · 14–24 bytes RAM      │  │
    │  └──────────────────────────────────────────────────────────┘  │
    │                         │                                       │
    │              Quantum Processor / Cryostat                       │
    └─────────────────────────────────────────────────────────────────┘

The stack is **modular**: L1 can be deployed standalone as a hardware shield, or combined with L2 for full algorithmic efficiency.

* * *

## Layer 1 — ORAC-NT / ORAC-QNode (Bare-Metal Hardware Shield)

ORAC-QNode is a deterministic real-time vitality controller that protects the **physical layer** surrounding quantum processors — thermal stability, power delivery aging, and component wear — without machine learning or lookup tables.

**Scope:** Acts as a predictive buffer for control electronics. It does NOT control qubits directly, but signals the host computer to adjust job scheduling based on real-time physical health.

Quantum hardware is sensitive to physical disturbances at the control layer:

* **Temperature fluctuations** — cryogenic drift and thermal spikes.
* **Component aging** — elevated Bit Error Rates (BER) as hardware accumulates wear.
* **Power delivery noise** — voltage transients affecting the quantum substrate.

### 🚀 New in Version 8 (v8 Upgrades)

* **Universal External Alert Bridge:** A dedicated, multi-threaded listener for intercepting external M2M (Machine-to-Machine) telemetry signals.
* **NASA GCN Compliance:** Full native support for NASA's General Coordinates Network (GCN) v7.0.0 JSON schema to detect transient events like Gravitational Waves in real time.
* **Deterministic Hardware Lock:** Instant hardware priority override that locks core vitality $W$ at `0.99` (`EMERGENCY_RESONANT`) during active alerts.
* **Phase-Locked Loop (PLL):** Adaptive phase synchronization with external quantum signals.

> 🌐 **Universal Interoperability:** ORAC-QNode intercepts external hardware anomalies and astrophysical transient alerts in real-time to force-protect quantum coherence.

### 🧪 Experimental Validation: Sensor-Agnostic Proof

We verified that the same pipeline maintains stability ($W > 0$) across three fundamentally different sensors:

1. **DS18B20** — Digital thermometer (slow thermal mass).
2. **MPU6050** — Accelerometer (fast vibration response).
3. **NV-center (emulated)** — Quantum sensor with 1/f noise.

**Result:** ✅ All sensors maintained identical sign during an extreme **120°C spike**, with the system returning to HEALTHY status in less than **2.5 seconds**.

### 📊 Status Matrix & Operational Boundaries

| Status | W Range | Coherence (v8) | Operational Response |
| --- | --- | --- | --- |
| **RESONANT** | ≥ 0.45 | ≥ 0.78 | Optimal operation; phase-locked via internal PLL. |
| **EMERGENCY** | Fixed 0.99 | Locked | Forced protection mode triggered by NASA/Industrial alerts. |
| **HEALTHY** | 0.30 – 1.00 | ≥ 0.60 | Normal operation; standard task scheduling. |
| **WARM** | 0.00 – 0.29 | 0.45 – 0.60 | Thermal drift detected; throttles SpinQit job shots. |
| **CRITICAL** | < −0.120 | < 0.30 | High damage vector; pauses job execution queues. |

* * *

## Layer 2 — GravOptAdaptiveE_QV (Algorithmic Efficiency Engine)

`GravOptAdaptiveE_QV` is a hardware-informed quantum variational optimizer — a **Parameter Gatekeeper** for VQE, QAOA, and VQLS circuits. It consumes the physical telemetry produced by L1 ($W$, $E_{norm}$, `status`) and uses it to suppress redundant quantum evaluations before they reach the chip.

### Mechanism

**W-Gate (shot suppression):** If $W$ drops below the HEALTHY threshold or enters CRITICAL/EMERGENCY status, the optimizer pauses the parameter update entirely — zero shots issued, $\theta$ preserved. The cryostat is protected from unnecessary thermal load during hardware stress.

**Shot Budget Scaling:** During degraded but non-critical conditions (WARM / low-HEALTHY), the shot count is scaled proportionally to $W$:

| L1 Status | $W$ Range | Shot Budget Factor |
| --- | --- | --- |
| RESONANT | ≥ 0.45 | 100% |
| HEALTHY | 0.30 – 0.44 | 70% |
| WARM | 0.00 – 0.29 | 40% |
| Sub-zero | < 0.00 | 10% |
| CRITICAL / EMERGENCY | —   | 0% (full pause) |

**Gradient Freeze Mask (hardware-informed):** Parameters with $|\partial\mathcal{L}/\partial\theta_i|$ below an adaptive quantile threshold are skipped per step. The freeze percentile is driven by $E_{norm}$ from L1:

| $E_{norm}$ (thermal load) | Freeze Percentile |
| --- | --- |
| < 0.20 | 10% frozen |
| 0.20 – 0.49 | 25% frozen |
| 0.50 – 0.79 | 45% frozen |
| ≥ 0.80 | 65% frozen |

**Momentum:** Classical exponential moving average for smooth convergence across gated steps.

### Gradient Methods

* **`parameter_shift`** (default) — Exact quantum gradient via the parameter-shift rule. Two evaluations per parameter. Recommended for real hardware.
* **`finite_diff`** — Finite difference approximation. Lower shot count, lower precision. Suitable for classical simulators or resource-constrained pilots.

### Validated Performance (100-step simulation, 6 parameters)

| Metric | Value |
| --- | --- |
| Loss trajectory | −1.54 → **−5.12** (monotonic convergence) |
| Final $\theta$ | ≈ ±π (global minimum reached) |
| Shots saved | **9,240 / 46,080 = 20.1%** |
| Steps skipped (CRITICAL gate, steps 51–60) | **10 / 100 = 10%** |
| Convergence after CRITICAL pause | ✅ Resumed and continued to global minimum |

The optimizer correctly halted during the simulated CRITICAL window ($W = -0.15$), preserved parameter state, and resumed convergence upon hardware recovery — demonstrating the core protection loop.

### Quick Start

    from GravOptAdaptiveE_QV import GravOptAdaptiveE_QV, ORACTelemetry
    
    # 1. Define your variational cost function
    def my_vqe_cost(theta, shots):
        # Replace with PennyLane QNode or Qiskit Estimator
        ...
    
    # 2. Connect to ORAC-NT telemetry (mock for testing)
    telemetry_fn = lambda: ORACTelemetry.mock(W=0.72, E_norm=0.18)
    
    # 3. Initialize and run
    opt = GravOptAdaptiveE_QV(
        cost_fn=my_vqe_cost,
        n_params=6,
        telemetry_fn=telemetry_fn,
        lr=0.05,
        base_shots=1024,
        gradient_method="parameter_shift",
    )
    
    report = opt.optimize(n_steps=200)
    print(f"Shot savings: {report['saving_pct']:.1f}%")
    print(f"Final loss:   {report['final_loss']:.5f}")

For real hardware integration, replace `ORACTelemetry.mock(...)` with a serial/SPI reader from `orac_single_node_v8.h`.

* * *

## Technical Structure

| File | Layer | Description |
| --- | --- | --- |
| `GravOptAdaptiveE_QV.py` | L2  | Quantum variational parameter gatekeeper — main algorithmic engine |
| `GravOptAdaptiveE.py` | L2 (classical) | Edge ML co-processor optimizer — fine-grain freeze for ARM controllers |
| `GravOptMini_v2.py` | L2 (classical) | TinyML ultra-lightweight optimizer for battery-constrained co-processors |
| `orac_spinqit_wrapper.py` | L1  | Python bridge for SpinQit (SpinQ Gemini) integration |
| `orac_minimal_demo_v8.c` | L1  | Core C engine — deterministic W(t) mathematics and sensor validation |
| `orac_single_node_v8.h` | L1  | Ultra-lightweight header (24 bytes RAM) for STM32F4 deployment |
| `orac_network_v7e.h/.ino` | L1  | Multi-node network variant |
| `gcn_alert_generator.py` | L1  | NASA GCN Schema v7.0.0 alert generator |
| `schema/` | L1  | GCN JSON schema definitions |

* * *

## Scientific Reference

**Core Repository:** [github.com/Kretski/ORAC-QNode](https://github.com/Kretski/ORAC-QNode)

**Official Theoretical Foundation DOI:** [10.5281/zenodo.19019599](https://doi.org/10.5281/zenodo.19019599)

*This DOI links to the preprint "ORAC-NT v5.x: Optimal and Stable FDIR Architecture for Autonomous Spacecraft and Critical Systems". The stability metrics established in v5.x have been mathematically extended to derive the vitality $W(t)$ framework for quantum hardware protection and the GravOptAdaptiveE_QV gating logic.*

**Author:** Dimitar Kretski, Independent ResearcherCenter for Hydro- and Aerodynamics, Bulgarian Academy of Sciences, Varna, BulgariaORCID: [0000-0001-5108-2243](https://orcid.org/0000-0001-5108-2243)

* * *

## License

This software is proprietary. Commercial use, deployment, or institutional evaluation requires an active license agreement.

* **Evaluation:** Free for personal non-commercial testing.
* **Academic:** Free with citation of DOI [10.5281/zenodo.19019599](https://doi.org/10.5281/zenodo.19019599) and author notification.
* **Commercial:** Requires written license — contact **kretski1@gmail.com**

Full license terms: [LICENSE](LICENSE)
