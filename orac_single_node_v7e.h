#ifndef ORAC_SINGLE_NODE_V7E_H
#define ORAC_SINGLE_NODE_V7E_H

/**
 * ORAC-NT v7e — Single Quantum Node Guardian
 * ==========================================
 * Fix v1.1:
 *   - Phase member replaced with SIN_LUT[64] (was sawtooth)
 *   - T_celsius now contributes directly to E via Arrhenius scaling
 *   - Honest comment: manages physical layer, not qubits directly
 *
 * U(t) = sigma_E + alpha * (1 - f_familiarity)
 * W(t) = Q_perf - T_norm*0.84 - E_norm*0.22 + phase*0.098 - kappa*U(t)
 *
 * Author : Dimitar Kretski
 * DOI    : 10.5281/zenodo.19019599
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ── КОНФИГУРАЦИЯ ─────────────────────────────────────── */
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

#define ORAC_STATUS_HEALTHY  0
#define ORAC_STATUS_WARM     1
#define ORAC_STATUS_THROTTLE 2
#define ORAC_STATUS_CRITICAL 3
#define ORAC_STATUS_DEAD     4

#define ORAC_CLAMP(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))

/* ── SIN LUT [64] — FIX: replaces sawtooth phase ───────── */
static const float ORAC_SIN_LUT[64] = {
     0.0000f,  0.0980f,  0.1951f,  0.2903f,
     0.3827f,  0.4714f,  0.5556f,  0.6344f,
     0.7071f,  0.7730f,  0.8315f,  0.8819f,
     0.9239f,  0.9569f,  0.9808f,  0.9952f,
     1.0000f,  0.9952f,  0.9808f,  0.9569f,
     0.9239f,  0.8819f,  0.8315f,  0.7730f,
     0.7071f,  0.6344f,  0.5556f,  0.4714f,
     0.3827f,  0.2903f,  0.1951f,  0.0980f,
     0.0000f, -0.0980f, -0.1951f, -0.2903f,
    -0.3827f, -0.4714f, -0.5556f, -0.6344f,
    -0.7071f, -0.7730f, -0.8315f, -0.8819f,
    -0.9239f, -0.9569f, -0.9808f, -0.9952f,
    -1.0000f, -0.9952f, -0.9808f, -0.9569f,
    -0.9239f, -0.8819f, -0.8315f, -0.7730f,
    -0.7071f, -0.6344f, -0.5556f, -0.4714f,
    -0.3827f, -0.2903f, -0.1951f, -0.0980f,
};

/* ── СТРУКТУРИ ────────────────────────────────────────── */
typedef struct {
    float   W;
    float   U_t;
    float   E_norm;
    float   sigma_E;
    uint8_t status;
    uint8_t sacrificed;
} ORAC_Result;

typedef struct {
    float   E_history[ORAC_HISTORY_LEN];
    uint8_t hist_idx;

    float   E;               /* термална енергия (RC модел + Arrhenius) */
    float   thermal_energy;  /* дългосрочна акумулация (v7e)            */
    float   W;
    uint8_t phase_idx;       /* FIX: LUT индекс вместо float phase      */
    uint8_t sacrificed;
} ORAC_SingleState;

/* ── ИНИЦИАЛИЗАЦИЯ ────────────────────────────────────── */
static inline void orac_single_init(ORAC_SingleState *s) {
    memset(s, 0, sizeof(ORAC_SingleState));
    s->W         = 1.0f;
    s->phase_idx = 0;
    for (int i = 0; i < ORAC_HISTORY_LEN; i++)
        s->E_history[i] = 0.0f;
}

/* ── СТАТИСТИКА ───────────────────────────────────────── */
static inline void orac_hist_stats(const ORAC_SingleState *s,
                                   float *mean_out, float *std_out) {
    float sum = 0.0f;
    for (int i = 0; i < ORAC_HISTORY_LEN; i++) sum += s->E_history[i];
    float mean = sum / ORAC_HISTORY_LEN;
    float var  = 0.0f;
    for (int i = 0; i < ORAC_HISTORY_LEN; i++) {
        float d = s->E_history[i] - mean;
        var += d * d;
    }
    *mean_out = mean;
    *std_out  = sqrtf(var / ORAC_HISTORY_LEN);
}

/* ── ГЛАВНА СТЪПКА ────────────────────────────────────── */
/*
 * HONEST SCOPE NOTE:
 * This function manages the PHYSICAL LAYER around the quantum processor
 * (temperature, power delivery, component aging). It does NOT control
 * qubits directly. It signals the host computer to adjust job scheduling.
 */
static inline ORAC_Result orac_single_step(ORAC_SingleState *s,
                                           float T_celsius,
                                           float load) {
    ORAC_Result r;
    r.sacrificed = 0;

    if (s->sacrificed) {
        r.W = -1.0f; r.U_t = 1.0f; r.E_norm = 1.0f;
        r.sigma_E = 0.0f; r.status = ORAC_STATUS_DEAD;
        return r;
    }

    /* 1. Термална динамика — FIX: T_celsius влияе директно в E
     *    RC модел: E = E*cooling + load*0.15 + Arrhenius_contribution */
    float cooling = (load > 0.01f) ? 0.90f : 0.80f;
    float arrh_contrib = 0.0f;
    if (T_celsius > ORAC_T_ONSET) {
        float excess = T_celsius - ORAC_T_ONSET;
        /* Линейна Arrhenius апроксимация */
        arrh_contrib = 2.1e-3f * (1.0f + excess * 0.072f);
    }
    s->E = s->E * cooling + load * 0.15f + arrh_contrib;
    s->E = ORAC_CLAMP(s->E, 0.0f, 2.0f);

    /* Дългосрочна термална акумулация (v7e E_norm) */
    float heat = T_celsius - ORAC_T_AMB;
    if (heat < 0.0f) heat = 0.0f;
    s->thermal_energy = s->thermal_energy * ORAC_ENERGY_DECAY + heat;
    s->thermal_energy = ORAC_CLAMP(s->thermal_energy, 0.0f, ORAC_ENERGY_MAX);
    float E_norm_long = s->thermal_energy / ORAC_ENERGY_MAX;

    /* Запис в историята */
    s->E_history[s->hist_idx] = s->E;
    s->hist_idx = (s->hist_idx + 1) % ORAC_HISTORY_LEN;

    /* 2. U(t) */
    float mean_E, sigma_E;
    orac_hist_stats(s, &mean_E, &sigma_E);
    float distance      = fabsf(s->E - mean_E);
    float f_familiarity = ORAC_CLAMP(1.0f - distance, 0.0f, 1.0f);
    float U_t           = sigma_E + ORAC_ALPHA_U * (1.0f - f_familiarity);

    /* 3. W(t) */
    float T_norm = ORAC_CLAMP(
        (T_celsius - ORAC_T_ONSET) / (ORAC_T_THROTTLE - ORAC_T_ONSET),
        0.0f, 1.38f);

    /* FIX: SIN_LUT вместо sawtooth */
    float phase_term = ORAC_SIN_LUT[s->phase_idx] * ORAC_PHASE_GAIN;
    s->phase_idx = (s->phase_idx + 1) & 63;

    /* E_norm: комбинация от краткосрочно (s->E) и дългосрочно (E_norm_long) */
    float E_norm = s->E * 0.7f + E_norm_long * 0.3f;

    float W = (ORAC_Q_PERF
               - T_norm    * 0.84f
               - E_norm    * 0.22f
               + phase_term
               - ORAC_KAPPA * U_t);

    s->W = ORAC_CLAMP(W, -1.0f, 1.0f);

    /* 4. Статус */
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

    r.W          = s->W;
    r.U_t        = U_t;
    r.E_norm     = E_norm;
    r.sigma_E    = sigma_E;
    r.sacrificed = s->sacrificed;
    return r;
}

#endif /* ORAC_SINGLE_NODE_V7E_H */
