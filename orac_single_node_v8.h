#ifndef ORAC_SINGLE_NODE_V8_H
#define ORAC_SINGLE_NODE_V8_H

/* ORAC-NT v8 — Resonance-Enhanced Quantum Node Guardian
 * 
 * New in v8:
 *   - PLL (phase-locked loop) with adaptive step
 *   - Coherence metric
 *   - Temperature-dependent phase error
 *   - Fast recovery after thermal spikes
 * 
 * Author: Dimitar Kretski
 * DOI: 10.5281/zenodo.19019599
 */

#include <math.h>
#include <stdint.h>

/* ─── CONFIGURATION ─────────────────────────────────────── */
#define ORAC_T_ONSET        55.0f
#define ORAC_T_THROTTLE     83.0f
#define ORAC_T_AMB          35.0f
#define ORAC_Q_PERF         1.0f
#define ORAC_PHASE_GAIN     0.098f
#define ORAC_KAPPA          0.85f
#define ORAC_ALPHA_U        0.40f
#define ORAC_CRITICAL_W    -0.70f
#define ORAC_HISTORY_LEN    30
#define ORAC_ENERGY_DECAY   0.9985f
#define ORAC_ENERGY_MAX     500.0f

#define ORAC_PHASE_KP       0.15f
#define ORAC_PHASE_KI       0.02f
#define ORAC_MAX_PHASE_ERROR 1.57f

#define ORAC_STATUS_HEALTHY  0
#define ORAC_STATUS_WARM     1
#define ORAC_STATUS_THROTTLE 2
#define ORAC_STATUS_CRITICAL 3
#define ORAC_STATUS_DEAD     4
#define ORAC_STATUS_RESONANT 5

#define ORAC_CLAMP(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))

/* ─── SIN LUT [64] ─────────────────────────────────────── */
static const float ORAC_SIN_LUT[64] = {
    0.0000f,0.0980f,0.1951f,0.2903f,0.3827f,0.4714f,0.5556f,0.6344f,
    0.7071f,0.7730f,0.8315f,0.8819f,0.9239f,0.9569f,0.9808f,0.9952f,
    1.0000f,0.9952f,0.9808f,0.9569f,0.9239f,0.8819f,0.8315f,0.7730f,
    0.7071f,0.6344f,0.5556f,0.4714f,0.3827f,0.2903f,0.1951f,0.0980f,
    0.0000f,-0.0980f,-0.1951f,-0.2903f,-0.3827f,-0.4714f,-0.5556f,-0.6344f,
    -0.7071f,-0.7730f,-0.8315f,-0.8819f,-0.9239f,-0.9569f,-0.9808f,-0.9952f,
    -1.0000f,-0.9952f,-0.9808f,-0.9569f,-0.9239f,-0.8819f,-0.8315f,-0.7730f,
    -0.7071f,-0.6344f,-0.5556f,-0.4714f,-0.3827f,-0.2903f,-0.1951f,-0.0980f
};

/* ─── STRUCTURES ────────────────────────────────────────── */
typedef struct {
    float   W;
    float   U_t;
    float   E_norm;
    float   sigma_E;
    float   coherence;
    uint8_t status;
    uint8_t sacrificed;
} ORAC_Result;

typedef struct {
    float   E_history[ORAC_HISTORY_LEN];
    uint8_t hist_idx;
    float   E;
    float   thermal_energy;
    float   W;
    uint8_t phase_idx;
    uint8_t sacrificed;
    float   phase_error;
    float   phase_integral;
    float   coherence;
    uint8_t resonance_lock;
} ORAC_SingleState;

/* ─── INITIALIZATION ────────────────────────────────────── */
static inline void orac_single_init(ORAC_SingleState *s) {
    for (int i = 0; i < ORAC_HISTORY_LEN; i++) s->E_history[i] = 0.0f;
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

/* ─── STATISTICS ────────────────────────────────────────── */
static inline void orac_hist_stats(const ORAC_SingleState *s,
                                   float *mean_out, float *std_out) {
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

/* ─── MAIN STEP FUNCTION ────────────────────────────────── */
static inline ORAC_Result orac_single_step(ORAC_SingleState *s,
                                           float T_celsius,
                                           float load,
                                           float time_sec) {
    ORAC_Result r;
    r.sacrificed = 0;
    
    if (s->sacrificed) {
        r.W = -1.0f; r.U_t = 1.0f; r.E_norm = 1.0f;
        r.sigma_E = 0.0f; r.coherence = 0.0f; r.status = ORAC_STATUS_DEAD;
        return r;
    }
    
    /* 1. Thermal dynamics */
    float cooling = (load > 0.01f) ? 0.90f : 0.80f;
    s->E = s->E * cooling + load * 0.15f;
    s->E = ORAC_CLAMP(s->E, 0.0f, 2.0f);
    
    /* 2. Long-term accumulation */
    float heat = T_celsius - ORAC_T_AMB;
    if (heat < 0.0f) heat = 0.0f;
    s->thermal_energy = s->thermal_energy * ORAC_ENERGY_DECAY + heat;
    s->thermal_energy = ORAC_CLAMP(s->thermal_energy, 0.0f, ORAC_ENERGY_MAX);
    float E_norm_long = s->thermal_energy / ORAC_ENERGY_MAX;
    
    /* 3. History */
    s->E_history[s->hist_idx] = s->E;
    s->hist_idx = (s->hist_idx + 1) % ORAC_HISTORY_LEN;
    
    /* 4. U(t) - uncertainty */
    float mean_E, sigma_E;
    orac_hist_stats(s, &mean_E, &sigma_E);
    float distance = fabsf(s->E - mean_E);
    float f_familiarity = 1.0f - distance;
    if (f_familiarity < 0.0f) f_familiarity = 0.0f;
    if (f_familiarity > 1.0f) f_familiarity = 1.0f;
    float U_t = sigma_E + ORAC_ALPHA_U * (1.0f - f_familiarity);
    
    /* 5. PLL - phase error with temperature dependence */
    float temp_delta = (T_celsius - 45.0f) / 15.0f;
    temp_delta = ORAC_CLAMP(temp_delta, -1.0f, 1.0f);
    float phase_error_raw = 0.35f * temp_delta + 0.15f * sinf(time_sec * 0.7f);
    
    s->phase_integral += phase_error_raw;
    s->phase_integral = ORAC_CLAMP(s->phase_integral, -2.0f, 2.0f);
    
    float pll_output = ORAC_PHASE_KP * phase_error_raw + ORAC_PHASE_KI * s->phase_integral;
    s->phase_error = phase_error_raw - pll_output;
    s->phase_error = ORAC_CLAMP(s->phase_error, -ORAC_MAX_PHASE_ERROR, ORAC_MAX_PHASE_ERROR);
    
    /* 6. Coherence */
    float coherence_from_error = expf(-fabsf(s->phase_error) * 1.8f);
    s->coherence = coherence_from_error * 0.8f + 0.2f;
    s->coherence = ORAC_CLAMP(s->coherence, 0.0f, 1.0f);
    
    /* 7. Phase term with adaptive step */
    float phase_term = ORAC_SIN_LUT[s->phase_idx] * ORAC_PHASE_GAIN;
    float phase_step = 1.0f + ORAC_PHASE_KP * s->phase_error;
    phase_step = ORAC_CLAMP(phase_step, 0.5f, 1.5f);
    s->phase_idx = (s->phase_idx + (int)phase_step) & 63;
    
    /* 8. Normalizations */
    float T_norm = (T_celsius - ORAC_T_ONSET) / (ORAC_T_THROTTLE - ORAC_T_ONSET);
    T_norm = ORAC_CLAMP(T_norm, 0.0f, 1.38f);
    float E_norm = s->E * 0.7f + E_norm_long * 0.3f;
    
    /* 9. W(t) - core metric */
    float W = (ORAC_Q_PERF - T_norm * 0.84f - E_norm * 0.22f + phase_term - ORAC_KAPPA * U_t);
    s->W = ORAC_CLAMP(W, -1.0f, 1.0f);
    
    /* 10. Status */
    if (s->W < ORAC_CRITICAL_W) {
        s->sacrificed = 1;
        r.status = ORAC_STATUS_DEAD;
    } else if (s->W < -0.118f) {
        r.status = ORAC_STATUS_CRITICAL;
    } else if (s->W < 0.0f) {
        r.status = ORAC_STATUS_THROTTLE;
    } else if (s->W < 0.3f) {
        r.status = ORAC_STATUS_WARM;
    } else {
        r.status = ORAC_STATUS_HEALTHY;
    }
    
    r.W = s->W;
    r.U_t = U_t;
    r.E_norm = E_norm;
    r.sigma_E = sigma_E;
    r.coherence = s->coherence;
    r.sacrificed = s->sacrificed;
    
    return r;
}

#endif /* ORAC_SINGLE_NODE_V8_H */