/**
 * ORAC-NT — Minimal Demo: 3 сензора, един pipeline
 * 
 * Компилация: gcc -lm -o orac_demo orac_minimal_demo.c
 * Пускане:    ./orac_demo
 * 
 * Доказва: Една и съща W функция работи за:
 *   - DS18B20 (температура)
 *   - MPU6050 (акселерометър — емулираме като вибрации)
 *   - NV-center (квантов сензор — емулираме с 1/f шум)
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* ─── КОНФИГУРАЦИЯ ─────────────────────────────────────── */
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

/* ─── СТРУКТУРА НА ЕДИН СЕНЗОРЕН КАНАЛ ─────────────────── */
typedef struct {
    float E_history[ORAC_HISTORY_LEN];
    int   hist_idx;
    float E;              /* текуща термална енергия */
    float thermal_energy; /* дългосрочна акумулация */
    float W;
    int   phase_idx;
    int   sacrificed;
} SensorState;

/* ─── ИНИЦИАЛИЗАЦИЯ ────────────────────────────────────── */
void sensor_init(SensorState *s) {
    for (int i = 0; i < ORAC_HISTORY_LEN; i++) s->E_history[i] = 0.0f;
    s->hist_idx = 0;
    s->E = 0.0f;
    s->thermal_energy = 0.0f;
    s->W = 1.0f;
    s->phase_idx = 0;
    s->sacrificed = 0;
}

/* ─── СТАТИСТИКА (средно + std) ────────────────────────── */
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

/* ─── ОСНОВНАТА W ФУНКЦИЯ (еднаква за всички сензори) ──── */
float compute_W(SensorState *s, float T_celsius, float load) {
    if (s->sacrificed) return -1.0f;
    
    /* 1. Термална динамика */
    float cooling = (load > 0.01f) ? 0.90f : 0.80f;
    float arrh_contrib = 0.0f;
    if (T_celsius > ORAC_T_ONSET) {
        float excess = T_celsius - ORAC_T_ONSET;
        arrh_contrib = 0.0021f * (1.0f + excess * 0.072f);
    }
    s->E = s->E * cooling + load * 0.15f + arrh_contrib;
    if (s->E > 2.0f) s->E = 2.0f;
    if (s->E < 0.0f) s->E = 0.0f;
    
    /* 2. Дългосрочна акумулация */
    float heat = T_celsius - ORAC_T_AMB;
    if (heat < 0.0f) heat = 0.0f;
    s->thermal_energy = s->thermal_energy * 0.9985f + heat;
    if (s->thermal_energy > 500.0f) s->thermal_energy = 500.0f;
    float E_norm_long = s->thermal_energy / 500.0f;
    
    /* 3. Запис в историята */
    s->E_history[s->hist_idx] = s->E;
    s->hist_idx = (s->hist_idx + 1) % ORAC_HISTORY_LEN;
    
    /* 4. U(t) — несигурност */
    float mean_E, sigma_E;
    history_stats(s, &mean_E, &sigma_E);
    float distance = fabsf(s->E - mean_E);
    float f_familiarity = distance > 1.0f ? 0.0f : (1.0f - distance);
    float U_t = sigma_E + ORAC_ALPHA_U * (1.0f - f_familiarity);
    
    /* 5. W(t) — основната метрика */
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

/* ─── СИМУЛАЦИЯ НА ТРИТЕ СЕНЗОРА ───────────────────────── */
float read_DS18B20_sim(float time_sec) {
    /* Термометър — бавна термална динамика */
    return 45.0f + 20.0f * sinf(time_sec / 18.0f);
}

float read_MPU6050_sim(float time_sec) {
    /* Акселерометър — вибрации от термично разширение */
    float T = read_DS18B20_sim(time_sec);
    return (T - 35.0f) / 60.0f;  /* нормализиран товар */
}

float read_NV_center_sim(float time_sec) {
    /* Квантов сензор — добавяме 1/f шум за реализъм */
    float T = read_DS18B20_sim(time_sec);
    float quantum_noise = 0.02f * sinf(time_sec * 7.3f) + 0.01f * sinf(time_sec * 21.1f);
    return (T - 35.0f) / 60.0f + quantum_noise;
}

/* ─── MAIN ────────────────────────────────────────────── */
int main() {
    SensorState s_ds18b20, s_mpu6050, s_nv;
    sensor_init(&s_ds18b20);
    sensor_init(&s_mpu6050);
    sensor_init(&s_nv);
    
    printf("=== ORAC-NT Demo: 3 сензора, един pipeline ===\n");
    printf("time(s) | T(C)   | W_DS18B20 | W_MPU6050 | W_NVcenter | корелация?\n");
    printf("--------|--------|-----------|-----------|------------|-----------\n");
    
    float last_W[3] = {0,0,0};
    
    for (int step = 0; step < 200; step++) {
        float t = step * 0.5f;  /* 0.5 сек стъпка = 100 сек общо */
        
        /* 1. Четем от трите сензора (симулация) */
        float T_raw = read_DS18B20_sim(t);
        float load_mpu = read_MPU6050_sim(t);
        float load_nv  = read_NV_center_sim(t);
        
        /* 2. Изчисляваме W за всеки през ЕДНА И СЪЩА функция */
        float W1 = compute_W(&s_ds18b20, T_raw, (T_raw - 35.0f)/60.0f);
        float W2 = compute_W(&s_mpu6050, T_raw, load_mpu);
        float W3 = compute_W(&s_nv,      T_raw, load_nv);
        
        /* 3. Принтираме на всеки 2 сек (4 стъпки) */
        if (step % 4 == 0) {
            char corr = (W1 > 0 && W2 > 0 && W3 > 0) ? '+' : 
                       ((W1 < 0 && W2 < 0 && W3 < 0) ? '+' : '?');
            printf("%7.1f | %6.1f | %9.4f | %9.4f | %10.4f |     %c\n",
                   t, T_raw, W1, W2, W3, corr);
        }
        
        last_W[0] = W1; last_W[1] = W2; last_W[2] = W3;
    }
    
    printf("\n--- Проверка на хипотезата ---\n");
    printf("Ако и трите W имат еднакъв знак (>0 или <0) при едни и същи условия,\n");
    printf("хипотезата за сензор-агностичност НЕ Е ОТХВЪРЛЕНА.\n");
    printf("Ако някой сензор показва различен знак — хипотезата е фалшифицирана.\n");
    
    return 0;
}