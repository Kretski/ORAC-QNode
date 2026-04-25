/**
 * ORAC-NT v8 — FULL WORKING VERSION
 * 
 * Compilation: gcc -lm -o orac_full orac_full.c
 * Run:        ./orac_full
 */

#include <stdio.h>
#include <math.h>

/* ─── CONFIGURATION ───────────────────────────────────── */
#define ORAC_T_ONSET        55.0f
#define ORAC_T_THROTTLE     83.0f
#define ORAC_T_AMB          35.0f
#define ORAC_Q_PERF         1.0f
#define ORAC_KAPPA          0.85f
#define ORAC_ALPHA_U        0.40f
#define ORAC_HISTORY_LEN    30
#define ORAC_ENERGY_DECAY   0.9985f
#define ORAC_ENERGY_MAX     500.0f
#define ORAC_CRITICAL_W    -0.70f
#define ORAC_PHASE_GAIN     0.098f

#define ORAC_PHASE_KP       0.15f
#define ORAC_PHASE_KI       0.02f
#define ORAC_MAX_PHASE_ERROR 1.57f

#define ORAC_CLAMP(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))

/* ─── SIN LUT ─────────────────────────────────────────── */
static const float SIN_LUT[64] = {
    0.0000f,0.0980f,0.1951f,0.2903f,0.3827f,0.4714f,0.5556f,0.6344f,
    0.7071f,0.7730f,0.8315f,0.8819f,0.9239f,0.9569f,0.9808f,0.9952f,
    1.0000f,0.9952f,0.9808f,0.9569f,0.9239f,0.8819f,0.8315f,0.7730f,
    0.7071f,0.6344f,0.5556f,0.4714f,0.3827f,0.2903f,0.1951f,0.0980f,
    0.0000f,-0.0980f,-0.1951f,-0.2903f,-0.3827f,-0.4714f,-0.5556f,-0.6344f,
    -0.7071f,-0.7730f,-0.8315f,-0.8819f,-0.9239f,-0.9569f,-0.9808f,-0.9952f,
    -1.0000f,-0.9952f,-0.9808f,-0.9569f,-0.9239f,-0.8819f,-0.8315f,-0.7730f,
    -0.7071f,-0.6344f,-0.5556f,-0.4714f,-0.3827f,-0.2903f,-0.1951f,-0.0980f
};

/* ─── SENSOR STATE ────────────────────────────────────── */
typedef struct {
    float E_history[ORAC_HISTORY_LEN];
    int hist_idx;
    float E;
    float thermal_energy;
    float W;
    int phase_idx;
    int sacrificed;
    float phase_error;
    float phase_integral;
    float coherence;
    int resonance_lock;
} SensorState;

/* ─── INIT ────────────────────────────────────────────── */
void sensor_init(SensorState *s) {
    int i;
    for (i = 0; i < ORAC_HISTORY_LEN; i++) s->E_history[i] = 0.0f;
    s->hist_idx = 0;
    s->E = 0.0f;
    s->thermal_energy = 0.0f;
    s->W = 1.0f;
    s->phase_idx = 0;
    s->sacrificed = 0;
    s->phase_error = 0.0f;
    s->phase_integral = 0.0f;
    s->coherence = 1.0f;
    s->resonance_lock = 0;
}

/* ─── STATS ───────────────────────────────────────────── */
void history_stats(SensorState *s, float *mean_out, float *std_out) {
    float sum = 0.0f;
    int i;
    for (i = 0; i < ORAC_HISTORY_LEN; i++) sum += s->E_history[i];
    float mean = sum / ORAC_HISTORY_LEN;
    float var = 0.0f;
    for (i = 0; i < ORAC_HISTORY_LEN; i++) {
        float d = s->E_history[i] - mean;
        var += d * d;
    }
    *mean_out = mean;
    *std_out = sqrtf(var / ORAC_HISTORY_LEN);
}

/* ─── COMPUTE W ───────────────────────────────────────── */
float compute_W(SensorState *s, float T_celsius, float load, float time_sec) {
    if (s->sacrificed) return -1.0f;
    
    /* Thermal dynamics */
    float cooling = (load > 0.01f) ? 0.90f : 0.80f;
    s->E = s->E * cooling + load * 0.15f;
    if (s->E > 2.0f) s->E = 2.0f;
    
    /* Long-term accumulation */
    float heat = T_celsius - ORAC_T_AMB;
    if (heat < 0.0f) heat = 0.0f;
    s->thermal_energy = s->thermal_energy * ORAC_ENERGY_DECAY + heat;
    if (s->thermal_energy > ORAC_ENERGY_MAX) s->thermal_energy = ORAC_ENERGY_MAX;
    float E_norm_long = s->thermal_energy / ORAC_ENERGY_MAX;
    
    /* History */
    s->E_history[s->hist_idx] = s->E;
    s->hist_idx = (s->hist_idx + 1) % ORAC_HISTORY_LEN;
    
    /* U(t) */
    float mean_E, sigma_E;
    history_stats(s, &mean_E, &sigma_E);
    float distance = fabsf(s->E - mean_E);
    float f_familiarity = (1.0f - distance > 0) ? (1.0f - distance) : 0;
    if (f_familiarity > 1.0f) f_familiarity = 1.0f;
    float U_t = sigma_E + ORAC_ALPHA_U * (1.0f - f_familiarity);
    
    /* PLL - simple version */
    float temp_delta = (T_celsius - 45.0f) / 15.0f;
    if (temp_delta > 1.0f) temp_delta = 1.0f;
    if (temp_delta < -1.0f) temp_delta = -1.0f;
    float phase_error_raw = 0.35f * temp_delta + 0.15f * sinf(time_sec * 0.7f);
    
    s->phase_integral += phase_error_raw;
    if (s->phase_integral > 2.0f) s->phase_integral = 2.0f;
    if (s->phase_integral < -2.0f) s->phase_integral = -2.0f;
    
    float pll_output = ORAC_PHASE_KP * phase_error_raw + ORAC_PHASE_KI * s->phase_integral;
    s->phase_error = phase_error_raw - pll_output;
    if (s->phase_error > ORAC_MAX_PHASE_ERROR) s->phase_error = ORAC_MAX_PHASE_ERROR;
    if (s->phase_error < -ORAC_MAX_PHASE_ERROR) s->phase_error = -ORAC_MAX_PHASE_ERROR;
    
    /* Coherence */
    float coherence_from_error = expf(-fabsf(s->phase_error) * 1.8f);
    s->coherence = coherence_from_error * 0.8f + 0.2f;
    if (s->coherence > 1.0f) s->coherence = 1.0f;
    
    /* Phase term */
    float phase_term = SIN_LUT[s->phase_idx] * ORAC_PHASE_GAIN;
    float phase_step = 1.0f + ORAC_PHASE_KP * s->phase_error;
    if (phase_step < 0.5f) phase_step = 0.5f;
    if (phase_step > 1.5f) phase_step = 1.5f;
    s->phase_idx = (s->phase_idx + (int)phase_step) & 63;
    
    /* Norms */
    float T_norm = (T_celsius - ORAC_T_ONSET) / (ORAC_T_THROTTLE - ORAC_T_ONSET);
    if (T_norm < 0.0f) T_norm = 0.0f;
    if (T_norm > 1.38f) T_norm = 1.38f;
    
    float E_norm = s->E * 0.7f + E_norm_long * 0.3f;
    
    /* W(t) */
    float W = ORAC_Q_PERF - T_norm * 0.84f - E_norm * 0.22f + phase_term - ORAC_KAPPA * U_t;
    if (W > 1.0f) W = 1.0f;
    if (W < -1.0f) W = -1.0f;
    
    s->W = W;
    if (W < ORAC_CRITICAL_W) s->sacrificed = 1;
    
    return W;
}

/* ─── MAIN ────────────────────────────────────────────── */
int main() {
    SensorState s1, s2, s3;
    sensor_init(&s1);
    sensor_init(&s2);
    sensor_init(&s3);
    
    printf("=== ORAC-NT v8 FULL VERSION ===\n");
    printf("time(s) | T(C) |   W1   |   W2   |   W3   | STATUS\n");
    printf("--------|------|--------|--------|--------|--------\n");
    
    for (int step = 0; step < 100; step++) {
        float t = step * 0.5f;
        float T = 45.0f + 20.0f * sinf(t / 18.0f);
        
        /* Thermal spike */
        if (t > 30.0f && t < 35.0f) T = 120.0f;
        
        float load = (T - 35.0f) / 60.0f;
        if (load < 0) load = 0;
        if (load > 1) load = 1;
        
        float W1 = compute_W(&s1, T, load, t);
        float W2 = compute_W(&s2, T, load + 0.02f * sinf(t * 7.3f), t);
        float W3 = compute_W(&s3, T, load + 0.03f * sinf(t * 21.1f), t);
        
        const char* status = "HEALTHY";
        if (W1 < -0.7f) status = "DEAD";
        else if (W1 < 0) status = "THROTTLE";
        else if (W1 < 0.3f) status = "WARM";
        
        if (step % 5 == 0 || (t > 30 && t < 36)) {
            printf("%7.1f | %4.1f | %6.4f | %6.4f | %6.4f | %s\n",
                   t, T, W1, W2, W3, status);
            fflush(stdout);
        }
    }
    
    printf("\n=== Test Complete ===\n");
    return 0;
}