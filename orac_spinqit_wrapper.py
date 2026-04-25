"""
ORAC-NT v7e — Python Wrapper за SpinQit
========================================
Чете Serial изхода от STM32 (single или network)
и управлява SpinQit job-овете в реално време.

Употреба:
    python orac_spinqit_wrapper.py --port COM3 --mode single
    python orac_spinqit_wrapper.py --port COM3 --mode network
    python orac_spinqit_wrapper.py --demo         # без хардуер

ВАЖНО: ORAC-NT управлява физическия слой около quantum хардуера
(температура, wear, захранване) — не управлява qubits директно.
Системата сигнализира на host компютъра да регулира job scheduling.

Изисквания:
    pip install pyserial spinqit
"""

import serial
import argparse
import threading
import time
import random
from collections import deque
from datetime import datetime

# ─── SpinQit импорт ───────────────────────────────────────────
try:
    from spinqit import Circuit, generate_hamiltonian
    from spinqit.compiler import get_compiler
    from spinqit.backend import get_backend
    SPINQIT_AVAILABLE = True
except ImportError:
    SPINQIT_AVAILABLE = False
    print("[WARN] spinqit не е намерен — работим в demo режим")

# ─── КОНФИГУРАЦИЯ ─────────────────────────────────────────────
DEFAULT_PORT   = "COM3"
DEFAULT_BAUD   = 115200
SHOTS_NORMAL   = 200
SHOTS_THROTTLE = 64
SHOTS_MIN      = 16
PAUSE_SECONDS  = 3.0

# ─── ГЛОБАЛЕН СТАТУС ──────────────────────────────────────────
class OracStatus:
    def __init__(self):
        self.lock        = threading.Lock()
        self.status      = "HEALTHY"
        self.W           = 1.0
        self.U_t         = 0.0
        self.lambda_cur  = 0.0
        self.failed      = 0
        self.history     = deque(maxlen=50)
        self.last_update = time.time()
        self.mode        = "single"

    def _parse_value(self, token):
        """Parses both '0.412' and 'W:0.412' formats."""
        if ":" in token:
            return float(token.split(":", 1)[1])
        return float(token)

    def update_single(self, line):
        """
        Parses: STEP|T|W|U|E|STATUS
        Example: 42|67.3|0.4120|0.0870|0.5310|WARM
        """
        try:
            parts = line.strip().split("|")
            if len(parts) < 6:
                return
            with self.lock:
                self.W      = self._parse_value(parts[2])
                self.U_t    = self._parse_value(parts[3])
                self.status = parts[5].strip().split(":")[-1]
                self.history.append({
                    "t":      datetime.now().strftime("%H:%M:%S"),
                    "W":      self.W,
                    "U":      self.U_t,
                    "status": self.status,
                })
                self.last_update = time.time()
        except Exception:
            pass

    def update_network(self, line1, line2):
        """
        Parses: NET:OK|W:0.712|U:0.091|L:0.23|FAIL:0/4
        """
        try:
            parts = {}
            for tok in line1.strip().split("|"):
                if ":" in tok:
                    k, v = tok.split(":", 1)
                    parts[k.strip()] = v.strip()
            with self.lock:
                self.status     = parts.get("NET", "OK")
                self.W          = float(parts.get("W", 1.0))
                self.U_t        = float(parts.get("U", 0.0))
                self.lambda_cur = float(parts.get("L", 0.0))
                fail_str        = parts.get("FAIL", "0/1")
                self.failed     = int(fail_str.split("/")[0])
                self.last_update = time.time()
        except Exception:
            pass

    def get_shots(self):
        with self.lock:
            s = self.status
        if s in ("HEALTHY", "OK"):
            return SHOTS_NORMAL
        elif s in ("WARM", "WARN"):
            return SHOTS_THROTTLE
        return SHOTS_MIN

    def should_pause(self):
        with self.lock:
            s = self.status
        return s in ("CRITICAL", "EMERGENCY", "DEAD")

# ─── SERIAL READER ────────────────────────────────────────────
def serial_reader(port, baud, orac, stop_event):
    try:
        ser = serial.Serial(port, baud, timeout=1.0)
        print(f"[ORAC] Свързан към {port} @ {baud}")
        pending_net_line = None

        while not stop_event.is_set():
            try:
                raw  = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="ignore").strip()
                if not line or line.startswith("===") or line.startswith("STEP"):
                    continue

                if orac.mode == "single":
                    if "|" in line and not line.startswith("!"):
                        orac.update_single(line)
                        _print_status(orac)
                else:
                    if line.startswith("NET:"):
                        pending_net_line = line
                    elif line.startswith("N0:") and pending_net_line:
                        orac.update_network(pending_net_line, line)
                        _print_status(orac)
                        print(f"     {line}")
                        pending_net_line = None

            except serial.SerialException:
                break
        ser.close()

    except serial.SerialException as e:
        print(f"[SERIAL ERR] {e}")
        print("[DEMO] Превключвам на симулиран вход...")
        _demo_serial(orac, stop_event)

def _print_status(orac):
    with orac.lock:
        s = orac.status
        w = orac.W
        u = orac.U_t
    icons = {
        "HEALTHY": "✅", "WARM": "🟡", "THROTTLE": "🟠",
        "CRITICAL": "🔴", "DEAD": "💀",
        "OK": "✅", "WARN": "🟡", "ALERT": "🟠", "EMERGENCY": "🔴",
    }
    icon = icons.get(s, "❓")
    print(f"  {icon} STATUS:{s:12s} W:{w:+.3f}  U:{u:.3f}")

def _demo_serial(orac, stop_event):
    """Симулира STM32 изход без хардуер."""
    step = 0
    E    = 0.0
    while not stop_event.is_set():
        t      = step % 120
        load   = 0.5 + 0.4 * (1 if 40 <= t < 70 else 0)
        E      = E * 0.9 + load * 0.15
        E      = min(E, 2.0)
        T      = 35 + E * 30
        W      = max(-1.0, min(1.0, 1.0 - E * 0.8 + random.uniform(-0.05, 0.05)))
        U      = abs(random.uniform(0.05, 0.15))
        E_norm = E / 2.0

        if W > 0.3:       status = "HEALTHY"
        elif W > 0.0:     status = "WARM"
        elif W > -0.118:  status = "THROTTLE"
        else:             status = "CRITICAL"

        line = f"{step}|{T:.1f}|{W:.4f}|{U:.4f}|{E_norm:.4f}|{status}"
        orac.update_single(line)
        _print_status(orac)
        step += 1
        time.sleep(0.1)

# ─── SPINQIT JOB КОНТРОЛЕР ────────────────────────────────────
def build_grover_circuit():
    if not SPINQIT_AVAILABLE:
        return None
    c = Circuit(2)
    c.h(0); c.h(1)
    c.cz(0, 1)
    c.h(0); c.h(1)
    return c

def run_spinqit_loop(orac, stop_event, n_runs=20):
    """
    Управлява SpinQit jobs.
    ORAC регулира shots и паузите — не qubits директно.
    Логиката: при физически стрес → по-малко shots → пази coherence.
    """
    circuit = build_grover_circuit()

    for run in range(n_runs):
        if stop_event.is_set():
            break

        if orac.should_pause():
            print(f"\n[SPINQIT] Run {run+1}: ⏸  PAUSED ({orac.status}) — {PAUSE_SECONDS}s")
            time.sleep(PAUSE_SECONDS)
            continue

        shots = orac.get_shots()
        print(f"\n[SPINQIT] Run {run+1}/{n_runs} | shots={shots} | STATUS={orac.status}")

        if SPINQIT_AVAILABLE and circuit:
            try:
                compiler = get_compiler("local")
                exe      = compiler.compile(circuit, level=0)
                backend  = get_backend("spinq_gemini")
                task     = backend.execute(exe, task_params={"shots": shots})
                result   = task.result()
                print(f"[SPINQIT] Резултат: {result.counts}")
            except Exception as e:
                print(f"[SPINQIT] Грешка: {e}")
        else:
            time.sleep(0.5 + random.uniform(0, 0.3))
            fidelity = max(0.4, orac.W * 0.5 + 0.5)
            print(f"[SPINQIT] Demo | fidelity≈{fidelity:.3f} shots={shots}")

        time.sleep(0.2)

    print("\n[SPINQIT] Всички runs завършиха.")

# ─── MAIN ─────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="ORAC-NT v7e SpinQit Wrapper")
    parser.add_argument("--port",  default=DEFAULT_PORT)
    parser.add_argument("--baud",  default=DEFAULT_BAUD, type=int)
    parser.add_argument("--mode",  default="single", choices=["single","network"])
    parser.add_argument("--demo",  action="store_true")
    parser.add_argument("--runs",  default=20, type=int)
    args = parser.parse_args()

    print("=" * 55)
    print("  ORAC-NT v7e  ·  SpinQit Wrapper v1.1")
    print(f"  Mode: {args.mode}  |  Port: {args.port}")
    print(f"  SpinQit: {'OK' if SPINQIT_AVAILABLE else 'DEMO'}")
    print("  Scope: physical layer guardian (not qubit control)")
    print("=" * 55)

    orac      = OracStatus()
    orac.mode = args.mode
    stop      = threading.Event()

    port = "DEMO" if args.demo else args.port
    t_serial = threading.Thread(
        target=serial_reader,
        args=(port, args.baud, orac, stop),
        daemon=True
    )
    t_serial.start()
    time.sleep(1.0)

    try:
        run_spinqit_loop(orac, stop, n_runs=args.runs)
    except KeyboardInterrupt:
        print("\n[ORAC] Прекъснато.")
    finally:
        stop.set()
        time.sleep(0.5)
        print("[ORAC] Изключване.")

if __name__ == "__main__":
    main()
