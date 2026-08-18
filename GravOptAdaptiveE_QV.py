# -*- coding: utf-8 -*-
# GravOptAdaptiveE_QV.py
# Layer 2 — Algorithmic Efficiency Engine (ORAC Stack / GravOpt Ecosystem)
#
# Quantum Variational Parameter Gatekeeper
# =========================================
# Интегриран с W(t) и E_norm от ORAC-NT (L1 — Bare-Metal Hardware Shield).
# Адаптивно ограничава VQE/QAOA parameter updates и shot budget въз основа
# на физическото здраве на хардуера, измерено от L1.
#
# Поддържани backends: PennyLane (simulator, SpinQit, IBM, IQM)
#
# Автор: Dimitar Kretski — Independent Researcher, CHA-BAS, Varna, Bulgaria
# ORCID: 0000-0001-5108-2243
# Репозиториум: github.com/Kretski/ORAC-QNode
#
# Лиценз: Proprietary — за лицензиране: kretski1@gmail.com

from __future__ import annotations

import warnings
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np

# ---------------------------------------------------------------------------
# Опционален PennyLane import — деградира изящно ако не е инсталиран
# ---------------------------------------------------------------------------
try:
    import pennylane as qml
    _HAS_PENNYLANE = True
except ImportError:  # pragma: no cover
    _HAS_PENNYLANE = False
    warnings.warn(
        "PennyLane не е инсталиран. GravOptAdaptiveE_QV работи в "
        "'dry-run' режим — параметрите се обновяват без квантово изпълнение.",
        ImportWarning,
        stacklevel=2,
    )


# ===========================================================================
# 1. ORAC-NT Telemetry Interface (L1 → L2 bridge)
# ===========================================================================

@dataclass
class ORACTelemetry:
    """
    Телеметрия от ORAC-NT (Layer 1).

    Полетата отразяват изходния сигнал на W(t) формулата:
        W(t) = Q·D − χ(wear)·T_norm − E_norm·0.22 + phase·0.098 − κ·U(t)

    В реална интеграция тези стойности се четат от bare-metal C кода
    (orac_single_node_v8.h) чрез serial/SPI/shared-memory интерфейс.

    За симулация/тестване може да се използва ORACTelemetry.mock().
    """
    W: float          # Vitality score  [−∞ .. +∞], типично [−0.5 .. +1.5]
    E_norm: float     # Нормирана енергийна памет [0.0 .. 1.0]
    T_norm: float     # Нормирана температура      [0.0 .. 1.0]
    phase: float      # Фазова синхронизация       [0.0 .. 1.0]
    status: str       # "RESONANT" | "HEALTHY" | "WARM" | "CRITICAL" | "EMERGENCY"

    # Thresholds (от Status Matrix в README)
    W_RESONANT: float  = field(default=0.45,  init=False, repr=False)
    W_HEALTHY_LO: float = field(default=0.30, init=False, repr=False)
    W_CRITICAL: float  = field(default=-0.12, init=False, repr=False)

    @classmethod
    def mock(
        cls,
        W: float = 0.72,
        E_norm: float = 0.18,
        T_norm: float = 0.25,
        phase: float = 0.91,
        status: str = "RESONANT",
    ) -> "ORACTelemetry":
        """Генерира mock-телеметрия за тестване без реален хардуер."""
        return cls(W=W, E_norm=E_norm, T_norm=T_norm, phase=phase, status=status)

    @property
    def is_critical(self) -> bool:
        return self.W < self.W_CRITICAL or self.status == "CRITICAL"

    @property
    def is_emergency(self) -> bool:
        return self.status == "EMERGENCY"

    @property
    def shot_budget_factor(self) -> float:
        """
        Скалира shot budget според физическото здраве.

        W ≥ 0.45 (RESONANT)  → 1.00  (пълен бюджет)
        W ∈ [0.30, 0.45)     → 0.70  (умерено намаляване)
        W ∈ [0.00, 0.30)     → 0.40  (значително намаляване — WARM)
        W < 0.00             → 0.10  (минимален shot budget — почти стоп)
        EMERGENCY            → 0.00  (пълна пауза)
        """
        if self.is_emergency:
            return 0.0
        if self.W >= self.W_RESONANT:
            return 1.0
        if self.W >= self.W_HEALTHY_LO:
            return 0.70
        if self.W >= 0.0:
            return 0.40
        return 0.10

    @property
    def freeze_percentile(self) -> float:
        """
        Процент параметри за замразяване, базиран на E_norm.

        Висока E_norm (топлинен стрес) → замразяваме повече параметри.
        Логиката е аналогична на GravOptMini_v2, но управлявана от хардуера.

        E_norm ∈ [0.0, 0.2)  → 10%  замразени  (чист хардуер)
        E_norm ∈ [0.2, 0.5)  → 25%  замразени
        E_norm ∈ [0.5, 0.8)  → 45%  замразени
        E_norm ≥ 0.8         → 65%  замразени  (термален стрес)
        """
        e = self.E_norm
        if e < 0.2:
            return 10.0
        if e < 0.5:
            return 25.0
        if e < 0.8:
            return 45.0
        return 65.0


# ===========================================================================
# 2. Shot Budget Manager
# ===========================================================================

class ShotBudgetManager:
    """
    Управлява квантовия shot budget за VQE/QAOA стъпка.

    При нормална работа (W ≥ 0.45) изпълняваме пълния брой shots.
    При хардуерен стрес (W < 0.45) редуцираме shots пропорционално
    на shot_budget_factor от ORACTelemetry.

    Спестени shots = (1 − factor) × base_shots × брой_стъпки
    """

    def __init__(self, base_shots: int = 1024):
        self.base_shots = base_shots
        self._total_shots_used: int = 0
        self._total_shots_saved: int = 0
        self._steps: int = 0

    def compute_shots(self, telemetry: ORACTelemetry) -> int:
        """Връща адаптивния брой shots за текущата стъпка."""
        factor = telemetry.shot_budget_factor
        shots = max(int(self.base_shots * factor), 0)
        saved = self.base_shots - shots
        self._total_shots_used += shots
        self._total_shots_saved += saved
        self._steps += 1
        return shots

    @property
    def efficiency_report(self) -> Dict[str, float]:
        if self._steps == 0:
            return {}
        return {
            "total_steps": self._steps,
            "shots_used": self._total_shots_used,
            "shots_saved": self._total_shots_saved,
            "saving_pct": 100.0 * self._total_shots_saved
                          / (self._total_shots_used + self._total_shots_saved + 1e-9),
        }


# ===========================================================================
# 3. GravOptAdaptiveE_QV — Quantum Parameter Gatekeeper
# ===========================================================================

class GravOptAdaptiveE_QV:
    """
    GravOptAdaptiveE_QV — Layer 2 Quantum Variational Optimizer

    Адаптивен оптимизатор за параметрите θ на квантова вариационна верига
    (VQE / QAOA / VQLS и др.), управляван от физическата телеметрия на
    ORAC-NT (L1).

    Механизъм:
    -----------
    1. **W-gate**: При W < W_HEALTHY_LO → стъпката се прескача изцяло
       (shots = 0, θ не се обновяват). Защитава криостата от излишна работа.
    2. **Shot scaling**: Броят на quantum evaluations се мащабира според
       shot_budget_factor(W, E_norm) — виж ORACTelemetry.
    3. **Gradient freeze**: Параметри с |∂L/∂θ| под адаптивен праг
       (базиран на E_norm) не се обновяват — аналог на GravOptMini_v2,
       но за квантово пространство на параметрите.
    4. **Momentum**: Класически momentum за по-гладка конвергенция.

    Параметри:
    ----------
    cost_fn : Callable[[np.ndarray, int], float]
        Функция на цената f(θ, shots) → scalar.
        shots=0 означава "не изпълнявай".
    n_params : int
        Брой variational параметри (размер на θ).
    lr : float
        Learning rate.
    momentum : float
        Momentum коефициент (0.9 по подразбиране).
    base_shots : int
        Базов брой quantum evaluations при пълно здраве (W ≥ 0.45).
    gradient_method : str
        "parameter_shift"  — точен квантов градиент (2 evaluations/param)
        "finite_diff"      — апроксимация (по-малко shots, по-малко точен)
    epsilon : float
        Стъпка за finite_diff (rad).
    verbose : bool
        Печата диагностика на всеки 10 стъпки.

    Пример:
    -------
    >>> telemetry_fn = lambda: ORACTelemetry.mock(W=0.72, E_norm=0.18)
    >>> opt = GravOptAdaptiveE_QV(cost_fn=my_vqe_cost, n_params=6,
    ...                           telemetry_fn=telemetry_fn)
    >>> for step in range(100):
    ...     loss, skipped = opt.step()
    """

    def __init__(
        self,
        cost_fn: Callable[[np.ndarray, int], float],
        n_params: int,
        telemetry_fn: Callable[[], ORACTelemetry],
        lr: float = 0.05,
        momentum: float = 0.9,
        base_shots: int = 1024,
        gradient_method: str = "parameter_shift",
        epsilon: float = np.pi / 2,
        verbose: bool = True,
        theta_init: Optional[np.ndarray] = None,
    ):
        self.cost_fn = cost_fn
        self.n_params = n_params
        self.telemetry_fn = telemetry_fn
        self.lr = lr
        self.momentum = momentum
        self.gradient_method = gradient_method
        self.epsilon = epsilon
        self.verbose = verbose

        # Инициализация на параметрите
        if theta_init is not None:
            assert len(theta_init) == n_params
            self.theta = np.array(theta_init, dtype=float)
        else:
            rng = np.random.default_rng(42)
            self.theta = rng.uniform(-np.pi, np.pi, n_params)

        # State
        self._exp_avg = np.zeros(n_params)
        self._step = 0
        self._skipped_steps = 0
        self.shot_manager = ShotBudgetManager(base_shots=base_shots)
        self._loss_history: List[float] = []

    # ------------------------------------------------------------------
    # Gradient computation
    # ------------------------------------------------------------------

    def _compute_gradient(
        self, theta: np.ndarray, shots: int
    ) -> np.ndarray:
        """
        Изчислява градиента чрез Parameter-Shift Rule или Finite Differences.

        Parameter-Shift (точен за квантови вериги):
            ∂f/∂θ_i = [f(θ + ε·eᵢ) − f(θ − ε·eᵢ)] / (2·sin(ε))
            при ε = π/2: = [f(θ + π/2·eᵢ) − f(θ − π/2·eᵢ)] / 2

        Finite Diff (апроксимация, по-евтин в shots):
            ∂f/∂θ_i ≈ [f(θ + ε·eᵢ) − f(θ)] / ε
        """
        grad = np.zeros(self.n_params)
        eps = self.epsilon

        if self.gradient_method == "parameter_shift":
            for i in range(self.n_params):
                shift = np.zeros(self.n_params)
                shift[i] = eps
                f_plus = self.cost_fn(theta + shift, shots)
                f_minus = self.cost_fn(theta - shift, shots)
                # Parameter-shift rule (ε = π/2)
                grad[i] = (f_plus - f_minus) / (2.0 * np.sin(eps))
        else:  # finite_diff
            f0 = self.cost_fn(theta, shots)
            for i in range(self.n_params):
                shift = np.zeros(self.n_params)
                shift[i] = eps
                f_plus = self.cost_fn(theta + shift, shots)
                grad[i] = (f_plus - f0) / eps

        return grad

    # ------------------------------------------------------------------
    # Freeze mask (аналог на GravOptMini_v2, адаптиран за θ)
    # ------------------------------------------------------------------

    def _freeze_mask(
        self, grad: np.ndarray, freeze_percentile: float
    ) -> np.ndarray:
        """
        Връща булева маска: True = параметърът се обновява.

        Параметрите с |∂L/∂θ_i| под freeze_percentile-тия квантил
        се замразяват за тази стъпка.
        """
        if freeze_percentile <= 0.0:
            return np.ones(self.n_params, dtype=bool)
        q = freeze_percentile / 100.0
        threshold = np.quantile(np.abs(grad), q)
        threshold = max(threshold, 1e-8)
        return np.abs(grad) >= threshold

    # ------------------------------------------------------------------
    # Main step
    # ------------------------------------------------------------------

    def step(self) -> Tuple[Optional[float], bool]:
        """
        Изпълнява една стъпка на квантовата оптимизация.

        Връща:
            (loss, skipped)
            loss    : стойността на cost function или None ако е прескочена
            skipped : True ако стъпката е прескочена поради нисък W
        """
        self._step += 1
        telemetry = self.telemetry_fn()

        # ── W-Gate: пълна пауза при критично / emergency ──────────────
        if telemetry.is_emergency or telemetry.is_critical:
            self._skipped_steps += 1
            if self.verbose:
                print(
                    f"[GravOptQV] Step {self._step:4d} │ "
                    f"⛔ SKIP — W={telemetry.W:.3f} "
                    f"status={telemetry.status}"
                )
            return None, True

        # ── Shot budget ───────────────────────────────────────────────
        shots = self.shot_manager.compute_shots(telemetry)
        if shots == 0:
            self._skipped_steps += 1
            if self.verbose:
                print(
                    f"[GravOptQV] Step {self._step:4d} │ "
                    f"⚠ SHOT=0 — W={telemetry.W:.3f} "
                    f"E_norm={telemetry.E_norm:.3f}"
                )
            return None, True

        # ── Gradient computation ──────────────────────────────────────
        grad = self._compute_gradient(self.theta, shots)

        # ── Freeze mask (hardware-informed) ───────────────────────────
        active_mask = self._freeze_mask(grad, telemetry.freeze_percentile)
        frozen_count = int((~active_mask).sum())

        # ── Momentum update ───────────────────────────────────────────
        self._exp_avg = (
            self.momentum * self._exp_avg
            + (1.0 - self.momentum) * grad
        )

        # ── Parameter update (само активни параметри) ─────────────────
        delta = np.zeros(self.n_params)
        delta[active_mask] = -self.lr * self._exp_avg[active_mask]
        self.theta += delta

        # ── Loss evaluation ───────────────────────────────────────────
        loss = self.cost_fn(self.theta, shots)
        self._loss_history.append(loss)

        # ── Diagnostics ───────────────────────────────────────────────
        if self.verbose and (self._step % 10 == 0 or self._step == 1):
            rep = self.shot_manager.efficiency_report
            print(
                f"[GravOptQV] Step {self._step:4d} │ "
                f"Loss={loss:.5f} │ "
                f"W={telemetry.W:.3f} │ "
                f"shots={shots}/{self.shot_manager.base_shots} │ "
                f"frozen={frozen_count}/{self.n_params} │ "
                f"saved={rep.get('saving_pct', 0):.1f}%"
            )

        return loss, False

    # ------------------------------------------------------------------
    # Convenience: run N steps
    # ------------------------------------------------------------------

    def optimize(self, n_steps: int = 100) -> Dict:
        """
        Изпълнява n_steps стъпки и връща обобщен доклад.

        Пример:
        >>> report = opt.optimize(200)
        >>> print(report)
        """
        for _ in range(n_steps):
            self.step()

        rep = self.shot_manager.efficiency_report
        losses = [l for l in self._loss_history if l is not None]

        return {
            "final_theta": self.theta.copy(),
            "final_loss": losses[-1] if losses else None,
            "loss_history": losses,
            "total_steps": self._step,
            "skipped_steps": self._skipped_steps,
            "skip_rate_pct": 100.0 * self._skipped_steps / max(self._step, 1),
            **rep,
        }

    @property
    def summary(self) -> str:
        rep = self.shot_manager.efficiency_report
        losses = [l for l in self._loss_history if l is not None]
        return (
            f"GravOptAdaptiveE_QV │ steps={self._step} │ "
            f"skip_rate={100*self._skipped_steps/max(self._step,1):.1f}% │ "
            f"shots_saved={rep.get('saving_pct',0):.1f}% │ "
            f"final_loss={losses[-1]:.5f}" if losses else "no evaluations"
        )


# ===========================================================================
# 4. Demo / Self-Test (без реален квантов хардуер)
# ===========================================================================

def _demo_cost_fn(theta: np.ndarray, shots: int) -> float:
    """
    Синтетична cost function — симулира VQE energy landscape.

    Базирана на bounded trigonometric form, типична за parametric
    quantum circuits (PQC). Глобален минимум: E ≈ -n_params при θ_i = π.

    В реална употреба: замени с PennyLane QNode или Qiskit Estimator.

    f(θ) = Σ [cos(θ_i) + 0.3·sin(2θ_i + 0.5)] + shot_noise
    Обхват: приблизително [−n, +n], добре дефиниран градиент навсякъде.
    """
    if shots == 0:
        return float("nan")
    # Shot noise намалява с √shots — имитира квантова статистика
    noise = np.random.normal(0, 0.3 / max(shots ** 0.5, 1))
    energy = float(np.sum(np.cos(theta) + 0.3 * np.sin(2 * theta + 0.5)))
    return energy + noise


def _demo_telemetry_sequence() -> Callable[[], ORACTelemetry]:
    """
    Генерира динамична телеметрия за демонстрация:
    - Стъпки 1-30:  RESONANT (W=0.72)
    - Стъпки 31-50: WARM     (W=0.18, E_norm=0.6)
    - Стъпки 51-60: CRITICAL (W=-0.15)
    - Стъпки 61-80: HEALTHY  (W=0.38, E_norm=0.3)
    - Стъпки 81+:   RESONANT (W=0.80)
    """
    step_counter = [0]

    def fn() -> ORACTelemetry:
        step_counter[0] += 1
        s = step_counter[0]
        if s <= 30:
            return ORACTelemetry.mock(W=0.72, E_norm=0.18, status="RESONANT")
        elif s <= 50:
            return ORACTelemetry.mock(W=0.18, E_norm=0.60, status="WARM")
        elif s <= 60:
            return ORACTelemetry.mock(W=-0.15, E_norm=0.85, status="CRITICAL")
        elif s <= 80:
            return ORACTelemetry.mock(W=0.38, E_norm=0.30, status="HEALTHY")
        else:
            return ORACTelemetry.mock(W=0.80, E_norm=0.10, status="RESONANT")

    return fn


if __name__ == "__main__":
    print("=" * 65)
    print("  GravOptAdaptiveE_QV — Self-Test Demo")
    print("  ORAC Stack / GravOpt Ecosystem — Layer 2")
    print("  github.com/Kretski/ORAC-QNode")
    print("=" * 65)

    N_PARAMS = 6
    telemetry_fn = _demo_telemetry_sequence()

    opt = GravOptAdaptiveE_QV(
        cost_fn=_demo_cost_fn,
        n_params=N_PARAMS,
        telemetry_fn=telemetry_fn,
        lr=0.05,
        momentum=0.9,
        base_shots=512,
        gradient_method="parameter_shift",
        verbose=True,
    )

    report = opt.optimize(n_steps=100)

    print("\n" + "=" * 65)
    print("  EFFICIENCY REPORT")
    print("=" * 65)
    for k, v in report.items():
        if k not in ("final_theta", "loss_history"):
            print(f"  {k:<25} {v}")
    print(f"  final_theta              {np.round(report['final_theta'], 4)}")
    print("=" * 65)
    print("\n✅ Demo завърши успешно.")
    print("   За реална квантова верига: замени _demo_cost_fn с PennyLane QNode.")
    print("   За реална L1 телеметрия: замени _demo_telemetry_sequence с serial/SPI reader.")
