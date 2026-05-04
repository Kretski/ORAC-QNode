import json
from datetime import datetime, timezone

def generate_orac_gcn_notice(event_type, source_name, metric_score, latency_ns, status):
    """
    Генерира JSON съобщение, съвместимо с GCN Schema концепциите, 
    адаптирано за ORAC-NT / ORAC-QNode.
    """
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    
    notice = {
        "$schema": "https://gcn.nasa.gov/schema/main/gcn/notices/boom/alert.schema.json",
        "alert_datetime": timestamp,
        "mission": "ORAC-Guardian",
        "data": {
            "targets": [
                {
                    "event_name": source_name,
                    "trigger_type": event_type,
                    "classification_scores": {
                        "Anomaly_Probability": 0.99,
                        "Vitality_W": metric_score
                    },
                    "hardware_telemetry": {
                        "reaction_latency_ns": latency_ns,
                        "system_status": status
                    },
                    "gcn_crossmatch": [
                        { "ref_type": "QNODE_EDGE", "ref_instrument": "STM32F4", "ref_ID": "NODE_01" }
                    ]
                }
            ]
        }
    }
    
    filename = f"ORAC_ALERT_{source_name}_{timestamp.replace(':', '')}.json"
    
    with open(filename, 'w') as f:
        json.dump(notice, f, indent=2)
        
    print(f"✅ GCN Alert generated: {filename}")

# --- ТЕСТ 1: Квантов термичен шок (ORAC-QNode) ---
generate_orac_gcn_notice(
    event_type="THERMAL_SHOCK_120C",
    source_name="QNODE_V8",
    metric_score=0.15, # W пада до 0.15
    latency_ns=535,
    status="VETO_TRIGGERED_RECOVERING"
)

# --- ТЕСТ 2: Гравитационна вълна (ORAC-NT) ---
generate_orac_gcn_notice(
    event_type="BBH_MERGER_SNR588",
    source_name="ET_MDC1_42581",
    metric_score=18.3, # Kurtosis
    latency_ns=0, 
    status="ASTRONOMICAL_DETECTION"
)