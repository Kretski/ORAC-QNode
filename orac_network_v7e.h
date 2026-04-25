#ifndef ORAC_NETWORK_V7E_H
#define ORAC_NETWORK_V7E_H

/**
 * ORAC-NT v7e — Quantum Network Guardian
 * =======================================
 * За мрежа от N quantum node-а (SpinQ + симулатори).
 * Разширява single-node логиката с:
 *   - Adaptive coupling между node-ите (λ)
 *   - Мрежова жизненост W_net = mean(W_i)
 *   - Load balancing: hot nodes получават по-малко λ
 *   - Колективна саможертва: ако W_net < ORAC_NET_CRITICAL
 *
 * Употреба: STM32 управлява N сензора (I2C мултиплексор)
 * и изпраща командата към управляващия компютър.
 */

#include "orac_single_node_v7e.h"

/* ── МРЕЖОВА КОНФИГУРАЦИЯ ─────────────────────────────── */
#define ORAC_MAX_NODES       8        // Максимален брой node-а
#define ORAC_NET_CRITICAL   -0.20f   // Мрежов праг за аларма
#define ORAC_LAM_MIN         0.10f   // Минимален coupling
#define ORAC_LAM_MAX         0.40f   // Максимален coupling
#define ORAC_LB_ALPHA        0.30f   // Load balancing сила
#define ORAC_DT2_THRESH     -0.004f  // Праг за predictive λ
#define ORAC_HYST_RATE       0.15f   // Скорост на hysteresis

/* Мрежови статуси */
#define ORAC_NET_OK          0
#define ORAC_NET_WARN        1
#define ORAC_NET_ALERT       2
#define ORAC_NET_EMERGENCY   3

/* ── СТРУКТУРИ ────────────────────────────────────────── */
typedef struct {
    float W_net;          // Средна мрежова жизненост
    float U_net;          // Средна несигурност
    float lambda_cur;     // Текущ coupling коефициент
    uint8_t failed_nodes; // Брой node-а с W < 0
    uint8_t net_status;   // ORAC_NET_*
    uint8_t node_status[ORAC_MAX_NODES]; // Статус на всеки node
} ORAC_NetResult;

typedef struct {
    ORAC_SingleState nodes[ORAC_MAX_NODES];
    uint8_t  n_nodes;

    /* Predictive coupling история */
    float    W_net_history[ORAC_HISTORY_LEN];
    uint8_t  net_hist_idx;

    float    lambda;       // Текущ λ (с hysteresis)
    float    lambda_target;

    /* Load balancing тегла */
    float    lb_weights[ORAC_MAX_NODES];
} ORAC_NetState;

/* ── ИНИЦИАЛИЗАЦИЯ ────────────────────────────────────── */
static inline void orac_net_init(ORAC_NetState *ns, uint8_t n_nodes) {
    if (n_nodes > ORAC_MAX_NODES) n_nodes = ORAC_MAX_NODES;
    ns->n_nodes     = n_nodes;
    ns->lambda      = 0.0f;
    ns->lambda_target = 0.0f;
    ns->net_hist_idx  = 0;

    for (int i = 0; i < n_nodes; i++) {
        orac_single_init(&ns->nodes[i]);
        ns->lb_weights[i] = 1.0f;
    }
    for (int i = 0; i < ORAC_HISTORY_LEN; i++)
        ns->W_net_history[i] = 1.0f;
}

/* ── LOAD BALANCING UPDATE ────────────────────────────── */
static inline void orac_net_update_lb(ORAC_NetState *ns) {
    /* Node-и с ниска жизненост получават по-малко товар */
    float sum = 0.0f;
    for (int i = 0; i < ns->n_nodes; i++) {
        float w = ns->nodes[i].W;
        /* Горещ node → тегло намалява */
        ns->lb_weights[i] = (w < 0.3f)
            ? (1.0f - ORAC_LB_ALPHA)
            : (1.0f + ORAC_LB_ALPHA * 0.25f);
        sum += ns->lb_weights[i];
    }
    /* Нормализираме */
    for (int i = 0; i < ns->n_nodes; i++)
        ns->lb_weights[i] /= (sum / ns->n_nodes);
}

/* ── PREDICTIVE λ (порт от v3/v4) ────────────────────── */
static inline float orac_net_predictive_lam(ORAC_NetState *ns) {
    /* Линеен fit върху последните 10 точки */
    int win = 10;
    float sx=0, sy=0, sxy=0, sxx=0;
    for (int i = 0; i < win; i++) {
        int idx = ((int)ns->net_hist_idx - win + i + ORAC_HISTORY_LEN)
                  % ORAC_HISTORY_LEN;
        float y = ns->W_net_history[idx];
        sx  += i; sy  += y;
        sxy += i*y; sxx += i*i;
    }
    float slope = (win*sxy - sx*sy) / (win*sxx - sx*sx + 1e-9f);

    /* Таргет λ по slope */
    float target;
    if      (slope < ORAC_DT2_THRESH * 2.0f) target = ORAC_LAM_MAX;
    else if (slope < ORAC_DT2_THRESH)        target = ORAC_LAM_MAX * 0.55f;
    else if (slope < 0.0f)                   target = ORAC_LAM_MAX * 0.20f;
    else                                      target = ORAC_LAM_MIN * 0.25f;

    return target;
}

/* ── ГЛАВНА МРЕЖОВА СТЪПКА ────────────────────────────── */
static inline ORAC_NetResult orac_net_step(ORAC_NetState *ns,
                                           float T_celsius[],
                                           float loads[]) {
    ORAC_NetResult nr;
    nr.failed_nodes = 0;

    /* 1. Обновяване на всеки node */
    float W_sum = 0.0f, U_sum = 0.0f;
    for (int i = 0; i < ns->n_nodes; i++) {
        float load_i = loads[i] * ns->lb_weights[i];
        ORAC_Result r = orac_single_step(&ns->nodes[i],
                                          T_celsius[i], load_i);
        nr.node_status[i] = r.status;
        W_sum += r.W;
        U_sum += r.U_t;
        if (r.W < 0.0f) nr.failed_nodes++;
    }

    float W_net = W_sum / ns->n_nodes;
    float U_net = U_sum / ns->n_nodes;

    /* 2. Запис на W_net в историята */
    ns->W_net_history[ns->net_hist_idx] = W_net;
    ns->net_hist_idx = (ns->net_hist_idx + 1) % ORAC_HISTORY_LEN;

    /* 3. Predictive λ с hysteresis */
    ns->lambda_target = orac_net_predictive_lam(ns);
    ns->lambda += ORAC_HYST_RATE * (ns->lambda_target - ns->lambda);
    ns->lambda  = ORAC_CLAMP(ns->lambda, 0.0f, ORAC_LAM_MAX);

    /* 4. Load balancing update за следващата стъпка */
    orac_net_update_lb(ns);

    /* 5. Мрежов статус */
    uint8_t net_status;
    if      (W_net < ORAC_NET_CRITICAL)  net_status = ORAC_NET_EMERGENCY;
    else if (nr.failed_nodes > 0)        net_status = ORAC_NET_ALERT;
    else if (W_net < 0.4f)               net_status = ORAC_NET_WARN;
    else                                  net_status = ORAC_NET_OK;

    nr.W_net       = W_net;
    nr.U_net       = U_net;
    nr.lambda_cur  = ns->lambda;
    nr.net_status  = net_status;
    return nr;
}

/* ── МРЕЖОВ SERIAL ИЗХОД ──────────────────────────────── */
/*
 * Примерен изход на всеки 100ms:
 * NET:OK | W:0.712 | U:0.091 | L:0.23 | FAIL:0/4
 * NODE0:HEALTHY NODE1:WARM NODE2:HEALTHY NODE3:THROTTLE
 */
static const char* ORAC_NET_STATUS_STR[] = {
    "OK", "WARN", "ALERT", "EMERGENCY"
};

#endif /* ORAC_NETWORK_V7E_H */
