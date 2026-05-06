import serial
import argparse
import threading
import time
import json
import os
from datetime import datetime

# --- КОНФИГУРАЦИЯ ---
DEFAULT_PORT = "COM3"
DEFAULT_BAUD = 115200

class OracStatus:
    def __init__(self):
        self.W = 1.0
        self.status = "INIT"
        self.last_update = datetime.now()
        self.lock = threading.Lock()
        self.emergency_mode = False

# --- НОВО: Универсална система за аларми (NASA + Индустриални) ---
class UniversalAlertSystem:
    def __init__(self, orac_obj):
        self.orac = orac_obj
        self.running = True

    def process_external_signal(self, json_data):
        """Парсва сигнали от външни източници"""
        is_critical = False
        source = "Unknown"

        # 1. Проверка за NASA GCN (Scientific/NASA interest)
        targets = json_data.get("data", {}).get("targets", [])
        for t in targets:
            for match in t.get("gcn_crossmatch", []):
                if match.get("ref_type") == "GW": # Гравитационна вълна
                    is_critical = True
                    source = f"NASA GCN (Event: {match.get('ref_ID')})"

        # 2. Проверка за Индустриални повреди (Business/CFO interest)
        if json_data.get("alert_type") in ["POWER_SURGE", "COOLING_FAIL", "VIBRATION_PEAK"]:
            is_critical = True
            source = f"Industrial Safety System ({json_data.get('alert_type')})"

        if is_critical:
            with self.orac.lock:
                self.orac.emergency_mode = True
                self.orac.status = "RESONANT" # Превключваме на максимална защита
                self.orac.W = 0.99
            print(f"\n[🛡️ SHIELD ACTIVE] Critical alert from {source}!")
            print("[🛡️ SHIELD ACTIVE] ORAC-QNode forcing RESONANT mode for hardware protection.")

    def monitor_folder(self, folder_path="alerts"):
        """Следи папка за нови JSON файлове (симулация на мрежов поток)"""
        if not os.path.exists(folder_path):
            os.makedirs(folder_path)
        
        print(f"[*] Monitoring for external alerts in: /{folder_path}")
        while self.running:
            for filename in os.listdir(folder_path):
                if filename.endswith(".json"):
                    path = os.path.join(folder_path, filename)
                    try:
                        with open(path, 'r') as f:
                            data = json.load(f)
                        self.process_external_signal(data)
                        os.remove(path) # Изтриваме след обработка
                    except Exception as e:
                        print(f"Error parsing alert: {e}")
            time.sleep(1)

def serial_reader(port, baud, orac_obj):
    """Чете данните от STM32 платката"""
    if port == "DEMO":
        print("[DEMO] Симулиране на Serial връзка...")
        while True:
            with orac_obj.lock:
                if not orac_obj.emergency_mode:
                    orac_obj.W = 0.5 + (0.5 * (1.0 - time.time() % 10 / 10))
                    orac_obj.status = "HEALTHY" if orac_obj.W > 0.4 else "WARM"
            time.sleep(1)
        return

    try:
        ser = serial.Serial(port, baud, timeout=1)
        while True:
            line = ser.readline().decode('utf-8').strip()
            if line and "|" in line:
                # STEP|T|W|U|E|STATUS
                parts = line.split("|")
                if len(parts) >= 6:
                    with orac_obj.lock:
                        if not orac_obj.emergency_mode:
                            orac_obj.W = float(parts[2])
                            orac_obj.status = parts[5]
    except Exception as e:
        print(f"[ERROR] Serial: {e}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--demo", action="store_true")
    args = parser.parse_args()

    orac = OracStatus()
    alerts = UniversalAlertSystem(orac)

    # Стартираме нишките
    t_serial = threading.Thread(target=serial_reader, args=("DEMO" if args.demo else args.port, 115200, orac), daemon=True)
    t_alerts = threading.Thread(target=alerts.monitor_folder, daemon=True)

    t_serial.start()
    t_alerts.start()

    print("\nORAC-QNode Universal Guardian Active.")
    print("Press Ctrl+C to stop.\n")

    try:
        while True:
            with orac.lock:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] Status: {orac.status} | Vitality W: {orac.W:.4f}", end='\r')
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\nStopping...")

if __name__ == "__main__":
    main()