/**
 * ORAC-NT — Minimal Demo: 3 sensors, one pipeline (with extreme spike test)
 * 
 * Compilation: gcc -lm -o orac_minimal_demo orac_minimal_demo.c
 * Run:        ./orac_minimal_demo
 * 
 * This version adds an EXTREME THERMAL SPIKE (120°C) between 30-35 seconds
 * to test falsifiability — do the three W values have the same sign?
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* ─── CONFIGURATION ─────────────────────────────────────── */
#define ORAC_T_ONSET        55.0f
#define ORAC_T_THROTTLE     83.0f
#define ORAC_T_AMB          35.0f
#define ORAC_Q_PERF         1.0f
#define ORAC_KAPPA          0.85f
#define ORAC_ALPHA_U        0.40f
#define ORAC_HISTORY_LEN    30

/* ─── SIN LUT (64) ─────────────────────────────────────── */
static const float SIN_LUT[64] = {
     0.0000f,  0.0980f,  0.1951f,  0.2903f,  0.3827f,  0.4714f,  0.5556f,  0.6344f,
     0.7071f,  0.7730f,  0.8315f,  0.8819f,  0.9239f,  0.9569f,  0.9808f,  0.9952f,
     1.0000f,  0.9952f,  0.9808f,  0.9569f,  0.9239f,  0.8819f,  0.8315f,  0.7730f,
     0.7071f,  0.6344f,  0.5556f,  0.4714f,  0.3827f,  0.2903f,  0.1951f,  0.0980f,
     0.0000f, -0.0980f, -0.1951f, -0.2903f, -0.3827f, -0.4714f, -0.5556f, -0.6344f,
    -0.7071f, -0.7730f, -0.8315f, -0.8819f, -0.9239f, -0.9569f, -0.9808f, -0.9952f,
    -1.0000f, -0.9952f, -0.9808f, -0.9569f, -0.9239f, -0.8819f, -0.8315f, -0.7730f,
    -0.7071f, -0.6344f, -0.5556f, -0.4714f, -0.3827f, -0.2903f, -0.1951f, -0.0980f
};

/* ─── SENSOR STATE STRUCTURE ───────────────────────────── */
typedef struct {
    float E_history[ORAC_HISTORY_LEN];
    int   hist_idx;
    float E;              /* current thermal energy */
    float thermal_energy; /* long-term accumulation */
    float W;
    int   phase_idx;
    int   sacrificed;
} SensorState;

/* ─── INITIALIZATION ───────────────────────────────────── */
void sensor_init(SensorState *s) {
    for (int i = 0; i < ORAC_HISTORY_LEN; i++) s->E_history[i] = 0.0f;
    s->hist_idx = 0;
    s->E = 0.0f;
    s->thermal_energy = 0.0f;
    s->W = 1.0f;
    s->phase_idx = 0;
    s->sacrificed = 0;
}

/* ─── HISTORY STATISTICS (mean + std) ──────────────────── */
void history_stats(SensorState *s, float *mean_out, float *std_out) {
    float sum = 0.0f;
    for (int i = 0; i < ORAC_HISTORY_LEN; i++) sum += s->E_history[i];
    float mean = sum / ORAC_HISTORY_LEN;
    float var = 0.0f;
    for (int i = 0; i < ORAC_HISTORY_LEN; i++) {
        float d = s->E_history[i] - mean;
        var += d * d;
    }
    *mean_out = mean;
    *std_out = sqrtf(var / ORAC_HISTORY_LEN);
}

/* ─── MAIN W FUNCTION (identical for all sensors) ──────── */
float compute_W(SensorState *s, float T_celsius, float load) {
    if (s->sacrificed) return -1.0f;
    
    /* 1. Thermal dynamics */
    float cooling = (load > 0.01f) ? 0.90f : 0.80f;
    float arrh_contrib = 0.0f;
    if (T_celsius > ORAC_T_ONSET) {
        float excess = T_celsius - ORAC_T_ONSET;
        arrh_contrib = 0.0021f * (1.0f + excess * 0.072f);
    }
    s->E = s->E * cooling + load * 0.15f + arrh_contrib;
    if (s->E > 2.0f) s->E = 2.0f;
    if (s->E < 0.0f) s->E = 0.0f;
    
    /* 2. Long-term accumulation */
    float heat = T_celsius - ORAC_T_AMB;
    if (heat < 0.0f) heat = 0.0f;
    s->thermal_energy = s->thermal_energy * 0.9985f + heat;
    if (s->thermal_energy > 500.0f) s->thermal_energy = 500.0f;
    float E_norm_long = s->thermal_energy / 500.0f;
    
    /* 3. Save to history */
    s->E_history[s->hist_idx] = s->E;
    s->hist_idx = (s->hist_idx + 1) % ORAC_HISTORY_LEN;
    
    /* 4. U(t) — uncertainty */
    float mean_E, sigma_E;
    history_stats(s, &mean_E, &sigma_E);
    float distance = fabsf(s->E - mean_E);
    float f_familiarity = distance > 1.0f ? 0.0f : (1.0f - distance);
    float U_t = sigma_E + ORAC_ALPHA_U * (1.0f - f_familiarity);
    
    /* 5. W(t) — the core metric */
    float T_norm = (T_celsius - ORAC_T_ONSET) / (ORAC_T_THROTTLE - ORAC_T_ONSET);
    if (T_norm < 0.0f) T_norm = 0.0f;
    if (T_norm > 1.38f) T_norm = 1.38f;
    
    float phase_term = SIN_LUT[s->phase_idx] * 0.098f;
    s->phase_idx = (s->phase_idx + 1) & 63;
    
    float E_norm = s->E * 0.7f + E_norm_long * 0.3f;
    
    float W = (ORAC_Q_PERF - T_norm * 0.84f - E_norm * 0.22f + phase_term - ORAC_KAPPA * U_t);
    if (W > 1.0f) W = 1.0f;
    if (W < -1.0f) W = -1.0f;
    
    if (W < -0.70f) s->sacrificed = 1;
    
    return W;
}

/* ─── SENSOR SIMULATIONS ───────────────────────────────── */

/* DS18B20 — slow thermal mass (temperature sensor) */
float read_DS18B20_sim(float time_sec) {
    float base = 45.0f + 20.0f * sinf(time_sec / 18.0f);
    
    /* EXTREME SPIKE: 120°C between 30-35 seconds */
    if (time_sec > 30.0f && time_sec < 35.0f) {
        base = 120.0f;
    }
    return base;
}

/* MPU6050 — accelerometer, responds instantly to vibration */
float read_MPU6050_sim(float time_sec) {
    float T = read_DS18B20_sim(time_sec);
    float load = (T - 35.0f) / 60.0f;
    if (load < 0.0f) load = 0.0f;
    if (load > 1.0f) load = 1.0f;
    return load;
}

/* NV-center — quantum sensor with 1/f noise and spike sensitivity */
float read_NV_center_sim(float time_sec) {
    float T = read_DS18B20_sim(time_sec);
    float quantum_noise = 0.02f * sinf(time_sec * 7.3f) + 0.01f * sinf(time_sec * 21.1f);
    float load = (T - 35.0f) / 60.0f + quantum_noise;
    
    /* During spike, NV-center shows faster response (quantum decoherence) */
    if (time_sec > 30.0f && time_sec < 35.0f) {
        load += 0.15f * sinf((time_sec - 30.0f) * 20.0f);  // fast oscillations
    }
    
    if (load < 0.0f) load = 0.0f;
    if (load > 1.2f) load = 1.2f;
    return load;
}

/* ─── MAIN ────────────────────────────────────────────── */
int main() {
    SensorState s_ds18b20, s_mpu6050, s_nv;
    sensor_init(&s_ds18b20);
    sensor_init(&s_mpu6050);
    sensor_init(&s_nv);
    
    printf("=== ORAC-NT Demo: 3 sensors, one pipeline ===\n");
    printf("*** EXTREME SPIKE TEST: 120°C at 30-35 sec ***\n");
    printf("time(s) | T(C)   | W_DS18B20 | W_MPU6050 | W_NVcenter | same sign?\n");
    printf("--------|--------|-----------|-----------|------------|-----------\n");
    
    int mismatch_count = 0;
    
    for (int step = 0; step < 250; step++) {
        float t = step * 0.4f;  /* 0.4 sec steps = 100 sec total */
        
        /* 1. Read from three sensors (simulation) */
        float T_raw = read_DS18B20_sim(t);
        float load_mpu = read_MPU6050_sim(t);
        float load_nv  = read_NV_center_sim(t);
        float load_ds = (T_raw - 35.0f) / 60.0f;
        if (load_ds < 0.0f) load_ds = 0.0f;
        if (load_ds > 1.0f) load_ds = 1.0f;
        
        /* 2. Compute W for each through THE SAME function */
        float W1 = compute_W(&s_ds18b20, T_raw, load_ds);
        float W2 = compute_W(&s_mpu6050, T_raw, load_mpu);
        float W3 = compute_W(&s_nv,      T_raw, load_nv);
        
        /* 3. Check sign agreement */
        int sign1 = (W1 > 0) ? 1 : -1;
        int sign2 = (W2 > 0) ? 1 : -1;
        int sign3 = (W3 > 0) ? 1 : -1;
        int all_same = (sign1 == sign2 && sign2 == sign3);
        
        if (!all_same) mismatch_count++;
        
        /* 4. Print every 5 steps (2 seconds) */
        if (step % 5 == 0 || !all_same) {
            char marker = all_same ? '+' : '!';
            if (!all_same) printf(">>> MISMATCH DETECTED! <<<\n");
            printf("%7.1f | %6.1f | %9.4f | %9.4f | %10.4f |     %c\n",
                   t, T_raw, W1, W2, W3, marker);
        }
    }
    
    printf("\n--- HYPOTHESIS TEST RESULT ---\n");
    if (mismatch_count == 0) {
        printf("✅ All W values have the same sign throughout the test.\n");
        printf("   Hypothesis NOT falsified.\n");
    } else {
        printf("❌ Found %d mismatches where signs differed.\n", mismatch_count);
        printf("   Hypothesis IS FALSIFIED — sensors do NOT agree under extreme conditions.\n");
        printf("   This is a VALID SCIENTIFIC OUTCOME.\n");
    }
    
    return 0;
}