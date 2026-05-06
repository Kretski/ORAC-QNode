"""
ORAC-QNode v8 — Python Bridge for SpinQit
==========================================
Plug & Play с orac_minimal_demo_v8.c

Протокол (v8 формат):
  C → Python:  STEP|TEMP|LOAD|W|COH|U_t|STATUS
  Python → C:  CMD:FORCE_RESONANT / CMD:RELEASE_RESONANT (UART)

Употреба:
  python orac_spinqit_wrapper.py --port COM3        # STM32
  python orac_spinqit_wrapper.py --port COM3 --net  # Network mode
  python orac_spinqit_wrapper.py --demo             # без хардуер

Изисквания:
  pip install pyserial spinqit
"""

import serial, argparse, threading, time, random, subprocess, os
from collections import deque
from datetime import datetime

# ── SpinQit (graceful fallback) ───────────────────────────────
try:
    from spinqit import Circuit
    from spinqit.compiler import get_compiler
    from spinqit.backend import get_backend
    SPINQIT_OK = True
except ImportError:
    SPINQIT_OK = False
    print("[WARN] spinqit not found — running in demo mode")

# ── Конфигурация ──────────────────────────────────────────────
DEFAULT_PORT  = "COM3"
DEFAULT_BAUD  = 115200
SHOTS = {
    "RESONANT":           200,
    "EMERGENCY_RESONANT": 200,   # пълна мощност при GCN защита
    "HEALTHY":            200,
    "WARM":                64,
    "THROTTLE":            16,
    "CRITICAL":             0,   # пауза
    "DEAD":                 0,
}
PAUSE_SEC = 3.0

# ── Статус обект ──────────────────────────────────────────────
class OracStatus:
    def __init__(self):
        self.lock    = threading.Lock()
        self.status  = "HEALTHY"
        self.W       = 1.0
        self.COH     = 1.0
        self.U_t     = 0.0
        self.step    = 0
        self.temp    = 0.0
        self.history = deque(maxlen=60)
        self.updated = time.time()

    def parse(self, line):
        """
        Парсва v8 формат: STEP|TEMP|LOAD|W|COH|U_t|STATUS
        Робустен към header редове и UART съобщения.
        """
        line = line.strip()
        if not line or "|" not in line:
            return False
        # Игнорира header/separator редове
        if line.startswith("STEP") or line.startswith("---") or \
           line.startswith("=") or line.startswith("["):
            return False
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 7:
            return False
        try:
            with self.lock:
                self.step   = int(parts[0])
                self.temp   = float(parts[1])
                # parts[2] = LOAD (не ни трябва)
                self.W      = float(parts[3])
                self.COH    = float(parts[4])
                self.U_t    = float(parts[5])
                self.status = parts[6]
                self.history.append({
                    "t":      datetime.now().strftime("%H:%M:%S"),
                    "step":   self.step,
                    "W":      self.W,
                    "COH":    self.COH,
                    "status": self.status,
                })
                self.updated = time.time()
            return True
        except (ValueError, IndexError):
            return False

    def get_shots(self):
        with self.lock:
            s = self.status
        return SHOTS.get(s, 0)

    def should_pause(self):
        with self.lock:
            s = self.status
        return SHOTS.get(s, 0) == 0

    def summary(self):
        with self.lock:
            return (self.status, self.W, self.COH, self.U_t, self.temp)

# ── Иконки ────────────────────────────────────────────────────
ICONS = {
    "RESONANT":           "🔵",
    "EMERGENCY_RESONANT": "🛰️ ",
    "HEALTHY":            "✅",
    "WARM":               "🟡",
    "THROTTLE":           "🟠",
    "CRITICAL":           "🔴",
    "DEAD":               "💀",
}

def print_status(orac):
    s, w, coh, u, t = orac.summary()
    icon = ICONS.get(s, "❓")
    shots = orac.get_shots()
    print(f"  {icon} {s:<20s} W:{w:+.4f}  COH:{coh:.3f}"
          f"  U:{u:.3f}  T:{t:.1f}°C  → shots:{shots}")

# ── Demo Serial (без хардуер) ─────────────────────────────────
def _demo_serial(orac, stop_event):
    """
    Стартира orac_minimal_demo_v8 като subprocess и чете изхода.
    Ако exe не е наличен, генерира синтетичен поток.
    """
    exe = os.path.join(os.path.dirname(__file__), "orac_demo_v8")
    if not os.path.exists(exe):
        # Windows
        exe_win = exe + ".exe"
        exe = exe_win if os.path.exists(exe_win) else None

    if exe:
        print(f"[DEMO] Стартирам {exe} като source...")
        try:
            proc = subprocess.Popen(
                [exe], stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL, text=True)
            for line in proc.stdout:
                if stop_event.is_set():
                    break
                if orac.parse(line):
                    print_status(orac)
                elif "[" in line:
                    print(f"  {line.strip()}")  # UART съобщения
            proc.wait()
            return
        except Exception as e:
            print(f"[DEMO] Subprocess грешка: {e} — синтетичен режим")

    # Fallback: синтетичен поток
    print("[DEMO] Синтетичен поток (компилирай orac_minimal_demo_v8.c за реален)")
    step = 0
    E = 0.0
    emergency = False
    em_count  = 0
    while not stop_event.is_set():
        load = 0.30
        E = min(2.0, E * 0.9 + load * 0.15)
        T = 45.0 + 5.0 * (((step % 100) - 50) / 50.0)
        W_phys = max(-1.0, min(1.0,
                    1.0 - max(0, (T-55)/(83-55))*0.84 - E*0.22
                    + 0.098 * (1 if step % 20 < 10 else -1)))
        COH = max(0.0, min(1.0, 0.5 + W_phys * 0.4))
        U   = round(random.uniform(0.005, 0.05), 3)

        if step == 50:
            print("\n[🛰️  UART RECEIVE] CMD:FORCE_RESONANT (Source: NASA GCN)")
            emergency = True; em_count = 0
        if step == 100:
            print("[🛰️  UART RECEIVE] CMD:RELEASE_RESONANT (Hardware Safe)\n")
            emergency = False; em_count = 0

        W_disp = W_phys
        if emergency:
            em_count += 1
            W_disp = max(0.85, min(0.99, W_phys + 0.15))
            if em_count >= 30:
                print("\n[⚠️  AUTO-TIMEOUT] EMERGENCY expired\n")
                emergency = False; em_count = 0
                W_disp = W_phys

        if emergency:
            status = "EMERGENCY_RESONANT"
        elif W_disp >= 0.45 and COH >= 0.78:
            status = "RESONANT"
        elif W_disp >= 0.30:
            status = "HEALTHY"
        elif W_disp >= 0.00:
            status = "WARM"
        elif W_disp >= -0.119:
            status = "THROTTLE"
        elif W_disp >= -0.699:
            status = "CRITICAL"
        else:
            status = "DEAD"

        line = f"{step}|{T:.1f}|{load:.2f}|{W_disp:.4f}|{COH:.3f}|{U:.3f}|{status}"
        if orac.parse(line):
            if step % 5 == 0 or emergency:
                print_status(orac)
        step += 1
        time.sleep(0.08)

# ── Serial reader thread ──────────────────────────────────────
def serial_reader(port, baud, orac, stop_event):
    if port == "DEMO":
        _demo_serial(orac, stop_event)
        return
    try:
        ser = serial.Serial(port, baud, timeout=1.0)
        print(f"[ORAC] Connected to {port} @ {baud}")
        while not stop_event.is_set():
            try:
                raw  = ser.readline()
                if not raw: continue
                line = raw.decode("utf-8", errors="ignore").strip()
                if not line: continue
                if orac.parse(line):
                    print_status(orac)
                elif line.startswith("["):
                    print(f"  {line}")
            except serial.SerialException:
                break
        ser.close()
    except serial.SerialException as e:
        print(f"[SERIAL ERR] {e}")
        print("[DEMO] Falling back to synthetic stream...")
        _demo_serial(orac, stop_event)

# ── SpinQit circuit ───────────────────────────────────────────
def build_circuit():
    if not SPINQIT_OK:
        return None
    c = Circuit(2)
    c.h(0); c.h(1)
    c.cz(0, 1)
    c.h(0); c.h(1)
    return c

# ── SpinQit job loop ──────────────────────────────────────────
def run_jobs(orac, stop_event, n_runs=20):
    circuit = build_circuit()
    for run in range(n_runs):
        if stop_event.is_set():
            break

        s, w, coh, _, _ = orac.summary()
        shots = orac.get_shots()

        if orac.should_pause():
            print(f"\n[SPINQIT] Run {run+1:>2}: ⏸  PAUSED"
                  f" — {s} — waiting {PAUSE_SEC}s")
            time.sleep(PAUSE_SEC)
            continue

        print(f"\n[SPINQIT] Run {run+1:>2}/{n_runs}"
              f" — shots={shots:>3}  W={w:+.4f}  {ICONS.get(s,'❓')} {s}")

        if SPINQIT_OK and circuit:
            try:
                exe    = get_compiler("local").compile(circuit, level=0)
                task   = get_backend("spinq_gemini").execute(
                             exe, task_params={"shots": shots})
                counts = task.result().counts
                print(f"[SPINQIT] Result: {counts}")
            except Exception as e:
                print(f"[SPINQIT] Error: {e}")
        else:
            time.sleep(0.4 + random.uniform(0, 0.2))
            fidelity = max(0.4, w * 0.45 + 0.5)
            print(f"[SPINQIT] Demo fidelity≈{fidelity:.3f}  shots={shots}")

        time.sleep(0.15)

    print("\n[SPINQIT] All runs complete.")

# ── Main ──────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(
        description="ORAC-QNode v8 — SpinQit Bridge")
    ap.add_argument("--port",  default=DEFAULT_PORT,
                    help="Serial port (COM3, /dev/ttyUSB0)")
    ap.add_argument("--baud",  default=DEFAULT_BAUD, type=int)
    ap.add_argument("--net",   action="store_true",
                    help="Network mode (multi-node)")
    ap.add_argument("--demo",  action="store_true",
                    help="Demo mode — no hardware needed")
    ap.add_argument("--runs",  default=20, type=int,
                    help="Number of SpinQit runs")
    args = ap.parse_args()

    print("=" * 58)
    print("  ORAC-QNode v8  ·  SpinQit Bridge")
    print(f"  Port: {args.port}  |  Baud: {args.baud}")
    print(f"  SpinQit: {'READY' if SPINQIT_OK else 'DEMO'}")
    print(f"  Protocol: STEP|TEMP|LOAD|W|COH|U_t|STATUS")
    print("=" * 58)

    orac  = OracStatus()
    stop  = threading.Event()
    port  = "DEMO" if args.demo else args.port

    t = threading.Thread(
        target=serial_reader,
        args=(port, args.baud, orac, stop),
        daemon=True)
    t.start()
    time.sleep(0.8)

    try:
        run_jobs(orac, stop, n_runs=args.runs)
    except KeyboardInterrupt:
        print("\n[ORAC] Interrupted.")
    finally:
        stop.set()
        time.sleep(0.3)
        print("[ORAC] Shutdown.")

if __name__ == "__main__":
    main()