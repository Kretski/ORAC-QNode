/**
 * ORAC-NT — Minimal Demo v8 (NASA & Industrial Alert Ready)
 * * Компилация: gcc -lm -o orac_demo_v8 orac_minimal_demo_v8.c
 * Пускане:    ./orac_demo_v8
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* --- КОНФИГУРАЦИЯ --- */
#define ORAC_Q_PERF         1.0f
#define ORAC_KAPPA          0.85f
#define ORAC_HISTORY_LEN    30

/* Глобални флагове за външно управление (от Python Моста) */
bool external_force_resonant = false;

typedef struct {
    float history[ORAC_HISTORY_LEN];
    int head;
    float phase;
    float coherence;
} ORAC_State;

/* Симулация на външна команда от NASA/CFO Bridge */
void check_external_commands(int step) {
    // Симулираме, че на стъпка 50 получаваме сигнал от NASA
    if (step == 50) {
        printf("\n[🛰️ UART RECEIVE] CMD:FORCE_RESONANT (Source: NASA GCN)\n");
        external_force_resonant = true;
    }
    // Симулираме, че на стъпка 100 опасността е преминала
    if (step == 100) {
        printf("\n[🛰️ UART RECEIVE] CMD:RELEASE_RESONANT (Hardware Safe)\n");
        external_force_resonant = false;
    }
}

float compute_W_v8(ORAC_State* s, float T, float load, float t) {
    /* Ако има външна аларма, игнорираме нормалните сензори и защитаваме хардуера */
    if (external_force_resonant) {
        s->coherence = 1.0f;
        return 0.99f; // Максимална жизненост в защитен режим
    }

    // Стандартна логика на ORAC-QNode
    float T_norm = (T - 35.0f) / 60.0f;
    float E_norm = load * 0.22f;
    float phase_effect = sinf(t + s->phase) * 0.098f;
    
    float W = ORAC_Q_PERF - T_norm - E_norm + phase_effect;
    
    if (W > 1.0f) W = 1.0f;
    if (W < -1.0f) W = -1.0f;
    
    return W;
}

const char* get_status(float W, bool resonance_active) {
    if (external_force_resonant) return "EMERGENCY_RESONANT";
    if (W > 0.45f) return "HEALTHY";
    if (W > 0.0f)  return "WARM";
    if (W > -0.5f) return "THROTTLE";
    return "CRITICAL";
}

int main() {
    ORAC_State node = {0};
    node.phase = 0.15f;

    printf("ORAC-QNode v8 Guardian — Physical Layer Protection\n");
    printf("Ready for NASA GCN & Industrial Alerts (UART Simulation)\n");
    printf("STEP | TEMP | LOAD |   W    | STATUS\n");
    printf("-----|------|------|--------|------------------\n");

    for (int step = 0; step < 150; step++) {
        float t = step * 0.1f;
        
        // 1. Проверка за външни команди (NASA Мост)
        check_external_commands(step);

        // 2. Симулация на сензори
        float T_raw = 45.0f + (sinf(t) * 5.0f); // Варираща температура
        float load = 0.3f;

        // 3. Изчисление
        float W = compute_W_v8(&node, T_raw, load, t);
        const char* status = get_status(W, external_force_resonant);

        // 4. Изход (това е, което Python Wrapper-ът чете)
        if (step % 5 == 0 || external_force_resonant) {
            printf("%4d | %4.1f | %4.2f | %6.4f | %s\n", 
                   step, T_raw, load, W, status);
        }

        // Кратка пауза (симулация)
    }

    return 0;
}