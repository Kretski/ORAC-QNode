import gradio as gr
import numpy as np
import matplotlib.pyplot as plt

def orac_w(temperature, load, t):
    """ORAC-NT W(t) симулация – времето t сега е аргумент"""
    T_ONSET = 55.0
    T_THROTTLE = 83.0
    
    # Нормализация на температурата
    if temperature < T_ONSET:
        T_norm = 0.0
    else:
        T_norm = (temperature - T_ONSET) / (T_THROTTLE - T_ONSET)
        T_norm = min(T_norm, 1.38)
    
    # Фазов член (зависи от времето)
    phase = np.sin(t * 0.5) * 0.098
    
    # W(t) формула
    W = 1.0 - T_norm * 0.84 - load * 0.22 + phase
    
    # Клипиране
    W = max(-1.0, min(1.0, W))
    
    return W

def get_status(W):
    """Връща статус и цвят на база W"""
    if W < -0.7:
        return "DEAD", "red"
    elif W < 0:
        return "THROTTLE", "orange"
    elif W < 0.3:
        return "WARM", "gold"
    elif W < 0.45:
        return "HEALTHY", "lightgreen"
    else:
        return "RESONANT", "green"

def demo(temperature, load):
    """Създава графика на W(t) за последните 50 секунди"""
    time_points = np.linspace(0, 50, 200)
    W_values = []
    
    for t in time_points:
        # Симулираме малки флуктуации на температурата и натоварването
        temp_sim = temperature + 2 * np.sin(t / 10)
        load_sim = load + 0.05 * np.sin(t / 5)
        W = orac_w(temp_sim, load_sim, t)
        W_values.append(W)
    
    # Изчисляваме текущия W и статус
    current_W = orac_w(temperature, load, 0)
    status, color = get_status(current_W)
    
    # Графика
    plt.figure(figsize=(12, 5))
    
    # Линия на W(t)
    plt.plot(time_points, W_values, linewidth=2.5, color='darkblue', label='W(t)')
    
    # Хоризонтални линии за праговете
    plt.axhline(y=0.45, color='green', linestyle='--', linewidth=1.5, label='RESONANT (W=0.45)')
    plt.axhline(y=0, color='gray', linestyle='--', linewidth=1)
    plt.axhline(y=-0.7, color='red', linestyle='--', linewidth=1.5, label='DEAD (W=-0.7)')
    
    # Оцветяване на зоните
    plt.fill_between(time_points, -1, W_values, 
                     where=np.array(W_values) > 0.45, 
                     color='green', alpha=0.25, label='RESONANT Zone')
    plt.fill_between(time_points, -1, W_values, 
                     where=np.array(W_values) < -0.7, 
                     color='red', alpha=0.25, label='DEAD Zone')
    
    plt.xlabel('Time (seconds)', fontsize=12)
    plt.ylabel('Vitality W(t)', fontsize=12)
    plt.title('ORAC-NT Real-Time Physical Layer Monitoring', fontsize=14)
    plt.ylim(-1, 1.1)
    plt.grid(True, alpha=0.3)
    plt.legend(loc='upper right')
    
    # Добавяме текстово поле с текущия статус
    plt.annotate(f'Current: T={temperature}°C, Load={load:.2f}\nW={current_W:.3f} | {status}', 
                 xy=(0.02, 0.95), xycoords='axes fraction', fontsize=11,
                 bbox=dict(boxstyle="round,pad=0.3", facecolor=color, alpha=0.7))
    
    return plt.gcf(), f"Current W = {current_W:.3f} | Status: {status}"

# ─── GRADIO ИНТЕРФЕЙС ─────────────────────────────────────
with gr.Blocks(title="ORAC-NT Quantum Node Guardian", theme=gr.themes.Soft()) as demo_interface:
    gr.Markdown("""
    # 🔷 ORAC-NT: Real-Time Physical Layer Guardian for Quantum Sensors
    
    **Sensor-agnostic stability metric** `W(t) = Q·D – T` | **535 ns latency** | **24 bytes RAM**
    
    *Move the sliders to see how the system responds to thermal stress and load changes*
    """)
    
    with gr.Row():
        with gr.Column(scale=1):
            temperature_slider = gr.Slider(
                25, 120, value=45, step=1, 
                label="🌡️ Temperature (°C)",
                info="Simulates NV-center thermal environment"
            )
            load_slider = gr.Slider(
                0, 1, value=0.5, step=0.05, 
                label="⚙️ System Load",
                info="Computational / RF load"
            )
            status_text = gr.Textbox(
                label="📊 Current Status", 
                interactive=False,
                value="HEALTHY"
            )
        
        with gr.Column(scale=2):
            plot_output = gr.Plot(label="Vitality Over Time")
    
    # Свързване на слайдерите с демото
    temperature_slider.change(demo, [temperature_slider, load_slider], [plot_output, status_text])
    load_slider.change(demo, [temperature_slider, load_slider], [plot_output, status_text])
    
    # Зареждане при старт
    demo_interface.load(demo, [temperature_slider, load_slider], [plot_output, status_text])
    
    gr.Markdown("""
    ---
    ### 📊 **Experimental Validation (120°C Spike Test)**
    
    | Metric | Result |
    |--------|--------|
    | Sensor-agnostic correlation | **W1 ≈ W2 ≈ W3** (error < 0.002) |
    | Recovery after 120°C spike | **< 2.5 seconds** |
    | RESONANT state (W > 0.45) | ✅ Detected |
    | Deterministic latency | **535 ns** |
    
    ### 🔗 **Resources**
    - **GitHub:** [github.com/Kretski/ORAC-QNode](https://github.com/Kretski/ORAC-QNode)
    - **DOI:** 10.5281/zenodo.19019599
    - **Author:** Dimitar Kretski | kretski1@gmail.com
    """)

# Стартиране
if __name__ == "__main__":
    demo_interface.launch()