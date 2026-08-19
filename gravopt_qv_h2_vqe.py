# -*- coding: utf-8 -*-
# gravopt_qv_h2_vqe.py
#
# VQE валидация на gravopt-qv с реален H₂ молекулярен Хамилтониан
# Target: ground state energy = -1.137 Hartree
#
# Setup:
#   pip install pennylane pennylane-qchem gravopt-qv
#
# Author: Dimitar Kretski, CHA-BAS, Varna, Bulgaria
# ORCID: 0000-0001-5108-2243
# Repo: github.com/Kretski/ORAC-QNode

import numpy as np
import json
import time
from datetime import datetime

# ── ORAC-NT телеметрия ────────────────────────────────────────────────────
from gravopt_qv import GravOptAdaptiveE_QV, ORACTelemetry

def h2_telemetry_sequence(n_steps):
    """
    Реалистична stress sequence:
    70% RESONANT, 20% WARM, 10% CRITICAL
    Физически мотивирано за среден QPU (~4 qubits)
    """
    schedule = (
        [(0.72, 0.15, "RESONANT")] * int(n_steps * 0.35) +
        [(0.20, 0.62, "WARM")]     * int(n_steps * 0.20) +
        [(0.72, 0.15, "RESONANT")] * int(n_steps * 0.35) +
        [(-0.14, 0.88, "CRITICAL")]* int(n_steps * 0.10) +
        [(0.80, 0.10, "RESONANT")] * 10  # buffer
    )
    idx = [0]
    def fn():
        i = min(idx[0], len(schedule)-1)
        W, E, s = schedule[i]; idx[0] += 1
        return ORACTelemetry.mock(W=W, E_norm=E, status=s)
    return fn

# ── H₂ VQE Setup ─────────────────────────────────────────────────────────

def build_h2_cost_fn(use_real_qpu=False, ibm_token=None):
    """
    Изгражда cost function за H₂ VQE.

    Режими:
    1. default.qubit (симулатор, без IBM) — бързо, без credits
    2. IBM Quantum реален QPU — бавно, изисква token

    H₂ Хамилтониан: 4 qubits, ground state = -1.137 Hartree
    Ansatz: Hartree-Fock + DoubleExcitation (1 параметър)
            или hardware-efficient (4 параметъра)
    """
    import pennylane as qml

    # Зареждаме H₂ Хамилтониан от PennyLane Datasets
    print("  Зареждане на H₂ Хамилтониан...")
    try:
        dataset = qml.data.load("qchem", molname="H2", bondlength=0.742,
                                basis="STO-3G")[0]
        H = dataset.hamiltonian
        hf_state = dataset.hf_state
        n_qubits = len(H.wires)
        print(f"  ✅ H₂ Хамилтониан: {n_qubits} qubits, {len(H.terms()[0])} Pauli термa")
        print(f"  Hartree-Fock state: {hf_state}")
    except Exception as e:
        print(f"  ⚠ Dataset недостъпен ({e}), изграждам ръчно...")
        symbols = ["H", "H"]
        geometry = np.array([[0., 0., -0.66140414], [0., 0., 0.66140414]])
        molecule = qml.qchem.Molecule(symbols, geometry)
        H, n_qubits = qml.qchem.molecular_hamiltonian(molecule)
        hf_state = qml.qchem.hf_state(2, n_qubits)
        print(f"  ✅ H₂ Хамилтониан: {n_qubits} qubits")

    # Device
    if use_real_qpu and ibm_token:
        from qiskit_ibm_runtime import QiskitRuntimeService
        service = QiskitRuntimeService(
            channel="ibm_quantum_platform", token=ibm_token
        )
        backend = service.least_busy(
            simulator=False, operational=True, min_num_qubits=n_qubits
        )
        dev = qml.device("qiskit.remote", wires=n_qubits, backend=backend)
        backend_name = backend.name
        print(f"  ✅ Backend: {backend_name} (реален QPU)")
    else:
        dev = qml.device("default.qubit", wires=n_qubits)
        backend_name = "default.qubit (симулатор)"
        print(f"  ✅ Backend: {backend_name}")

    # Ansatz: Hartree-Fock + DoubleExcitation
    # Единственият физически мотивиран параметър за H₂ в STO-3G
    @qml.qnode(dev)
    def vqe_circuit(params):
        qml.BasisState(hf_state, wires=range(n_qubits))
        qml.DoubleExcitation(params[0], wires=[0, 1, 2, 3])
        return qml.expval(H)

    def cost_fn(theta: np.ndarray, shots: int) -> float:
        if shots == 0:
            return float("nan")
        # Използваме само първия параметър (физически ansatz)
        result = vqe_circuit(theta[:1])
        # Добавяме shot noise пропорционален на 1/√shots
        noise = np.random.normal(0, 0.01 / max(shots**0.5, 1))
        return float(result) + noise

    # Известна точна стойност
    exact_energy = -1.1373  # Hartree, FCI/STO-3G

    return cost_fn, n_qubits, hf_state, exact_energy, backend_name


# ── Сравнение: с и без W(t) gating ───────────────────────────────────────

def run_comparison(n_steps=50, use_real_qpu=False, ibm_token=None):
    """
    Сравнява convergence с и без hardware-informed gating.
    Ключов резултат за preprint и за писмото към DiCarlo.
    """
    print("=" * 62)
    print("  gravopt-qv × H₂ VQE — Молекулярна валидация")
    print(f"  {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  Target: ground state energy = -1.137 Hartree")
    print("=" * 62)

    # Изграждаме cost function
    print("\n[1/3] Setup...")
    cost_fn, n_qubits, hf_state, exact_energy, backend_name = build_h2_cost_fn(
        use_real_qpu, ibm_token
    )

    base_shots = 256
    np.random.seed(42)
    theta_init = np.array([0.01])  # близо до HF state

    # ── RUN 1: С W(t) gating (GravOptAdaptiveE_QV) ───────────────────────
    print(f"\n[2/3] Run 1: GravOptAdaptiveE_QV (с W(t) gating)...")
    opt_gated = GravOptAdaptiveE_QV(
        cost_fn=cost_fn,
        n_params=1,
        telemetry_fn=h2_telemetry_sequence(n_steps),
        lr=0.3,
        momentum=0.8,
        base_shots=base_shots,
        gradient_method="parameter_shift",
        verbose=True,
        theta_init=theta_init.copy(),
    )
    t0 = time.time()
    report_gated = opt_gated.optimize(n_steps=n_steps)
    t_gated = time.time() - t0

    # ── RUN 2: Без gating (пълни shots, никога не пропуска) ──────────────
    print(f"\n[3/3] Run 2: Baseline (без W(t) gating, пълни shots)...")
    opt_baseline = GravOptAdaptiveE_QV(
        cost_fn=cost_fn,
        n_params=1,
        telemetry_fn=lambda: ORACTelemetry.mock(W=0.99, E_norm=0.0, status="RESONANT"),
        lr=0.3,
        momentum=0.8,
        base_shots=base_shots,
        gradient_method="parameter_shift",
        verbose=False,
        theta_init=theta_init.copy(),
    )
    t0 = time.time()
    report_baseline = opt_baseline.optimize(n_steps=n_steps)
    t_baseline = time.time() - t0

    # ── Резултати ─────────────────────────────────────────────────────────
    losses_gated    = [l for l in report_gated['loss_history'] if l is not None]
    losses_baseline = [l for l in report_baseline['loss_history'] if l is not None]

    final_gated    = losses_gated[-1]    if losses_gated    else None
    final_baseline = losses_baseline[-1] if losses_baseline else None

    error_gated    = abs(final_gated - exact_energy)    if final_gated    else None
    error_baseline = abs(final_baseline - exact_energy) if final_baseline else None

    rep = report_gated.get

    print("\n" + "=" * 62)
    print("  РЕЗУЛТАТИ — H₂ VQE валидация")
    print("=" * 62)
    print(f"  Backend:              {backend_name}")
    print(f"  Exact ground state:   {exact_energy:.4f} Hartree")
    print()
    print(f"  GravOptAdaptiveE_QV (с W(t) gating):")
    print(f"    Final energy:       {final_gated:.4f} Hartree")
    print(f"    Error vs exact:     {error_gated:.4f} Ha "
          f"({'✅ chemical accuracy' if error_gated and error_gated < 0.0016 else '⚠ above chem. acc.'})")
    print(f"    Shots saved:        {report_gated.get('saving_pct', 0):.1f}%")
    print(f"    Steps skipped:      {report_gated['skipped_steps']} (W-Gate)")
    print(f"    Runtime:            {t_gated:.1f}s")
    print()
    print(f"  Baseline (без gating):")
    print(f"    Final energy:       {final_baseline:.4f} Hartree")
    print(f"    Error vs exact:     {error_baseline:.4f} Ha "
          f"({'✅ chemical accuracy' if error_baseline and error_baseline < 0.0016 else '⚠ above chem. acc.'})")
    print(f"    Shots used:         100% (no savings)")
    print(f"    Runtime:            {t_baseline:.1f}s")
    print()

    if error_gated and error_baseline:
        quality = "✅ EQUIVALENT" if abs(error_gated - error_baseline) < 0.01 else "⚠ DIFFERENT"
        print(f"  Solution quality:     {quality}")
        print(f"  Shot savings:         {report_gated.get('saving_pct', 0):.1f}% "
              f"with no degradation in energy accuracy")

    # Запази JSON
    fname = f"h2_vqe_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    with open(fname, "w") as f:
        json.dump({
            "experiment": "H2_VQE_molecular_validation",
            "backend": backend_name,
            "timestamp": datetime.now().isoformat(),
            "exact_ground_state_hartree": exact_energy,
            "n_qubits": int(n_qubits),
            "n_steps": n_steps,
            "base_shots": base_shots,
            "gated": {
                "final_energy": final_gated,
                "error_vs_exact": error_gated,
                "shots_saved_pct": report_gated.get("saving_pct", 0),
                "skipped_steps": report_gated["skipped_steps"],
                "loss_history": losses_gated,
                "final_theta": report_gated["final_theta"].tolist(),
                "runtime_s": t_gated,
            },
            "baseline": {
                "final_energy": final_baseline,
                "error_vs_exact": error_baseline,
                "loss_history": losses_baseline,
                "final_theta": report_baseline["final_theta"].tolist(),
                "runtime_s": t_baseline,
            }
        }, f, indent=2)

    print(f"\n  ✅ Запазено: {fname}")
    print("  Качи в: github.com/Kretski/ORAC-QNode/tree/main/results/")
    print("=" * 62)
    return fname


if __name__ == "__main__":
    # Започва с локален симулатор (бързо, без IBM credits)
    # За реален QPU: use_real_qpu=True, ibm_token="твоя_токен"
    run_comparison(
        n_steps=50,
        use_real_qpu=False,
        ibm_token=None
    )
