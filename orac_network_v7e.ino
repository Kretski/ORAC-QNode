/**
 * ORAC-NT v7e — Network Guardian
 * ================================
 * STM32F4 / Arduino compatible .ino
 * За мрежа от 2-4 quantum node-а.
 *
 * Свързване (4 node-а):
 *   TCA9548A I2C мултиплексор → SDA/SCL
 *   DS18B20 × 4 → channel 0-3 на мултиплексора
 *   USB Serial → управляващ компютър (115200 baud)
 *
 * Изход по Serial (всеки 100ms):
 *   NET:OK|W:0.712|U:0.091|L:0.23|FAIL:0/4
 *   N0:HEALTHY|N1:WARM|N2:HEALTHY|N3:THROTTLE
 *
 * Python wrapper чете и управлява SpinQit jobs.
 */

#include "orac_network_v7e.h"

/* ── КОНФИГУРАЦИЯ ─────────────────────────────────────── */
#define SERIAL_BAUD   115200
#define LOOP_MS       100
#define N_NODES       4      // Брой активни node-а (макс 8)

/* ── ГЛОБАЛНО СЪСТОЯНИЕ ───────────────────────────────── */
ORAC_NetState NS;
uint32_t step_count = 0;
uint32_t last_print = 0;

/* ── СИМУЛИРАНИ ТЕМПЕРАТУРИ ───────────────────────────── */
/*
 * При реален деплоймент замени с четене от
 * DS18B20 сензори през TCA9548A мултиплексор.
 *
 * Пример с реален хардуер:
 * void select_channel(uint8_t ch) {
 *   Wire.beginTransmission(0x70);
 *   Wire.write(1 << ch);
 *   Wire.endTransmission();
 * }
 */
void read_temperatures(float T_out[], float load_out[], int n) {
    float t_sec = (float)millis() / 1000.0f;

    for (int i = 0; i < n; i++) {
        /* Всеки node има малко различна термална история */
        float base = 45.0f + 20.0f * sinf(t_sec / 18.0f + i * 0.8f);

        /* Node 1 получава spike около t=12s — симулира hot node */
        if (i == 1 && t_sec > 11.0f && t_sec < 14.0f)
            base += 35.0f;

        /* Node 3 е по-стабилен */
        if (i == 3) base -= 8.0f;

        T_out[i]    = base;
        load_out[i] = (base - 30.0f) / 60.0f;
        if (load_out[i] < 0.0f) load_out[i] = 0.0f;
        if (load_out[i] > 1.0f) load_out[i] = 1.0f;
    }
}

/* ── SETUP ────────────────────────────────────────────── */
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1500);

    orac_net_init(&NS, N_NODES);

    Serial.println(F("=== ORAC-NT v7e Network Guardian ==="));
    Serial.print(F("Nodes: ")); Serial.println(N_NODES);
    Serial.println(F("READY"));
}

/* ── MAIN LOOP ────────────────────────────────────────── */
void loop() {

    float T_arr[ORAC_MAX_NODES]    = {0};
    float load_arr[ORAC_MAX_NODES] = {0};

    read_temperatures(T_arr, load_arr, N_NODES);

    ORAC_NetResult nr = orac_net_step(&NS, T_arr, load_arr);

    if (millis() - last_print >= LOOP_MS) {
        last_print = millis();

        /* Ред 1: мрежов статус */
        Serial.print(F("NET:"));
        Serial.print(ORAC_NET_STATUS_STR[nr.net_status]);
        Serial.print(F("|W:"));   Serial.print(nr.W_net, 3);
        Serial.print(F("|U:"));   Serial.print(nr.U_net, 3);
        Serial.print(F("|L:"));   Serial.print(nr.lambda_cur, 3);
        Serial.print(F("|FAIL:")); Serial.print(nr.failed_nodes);
        Serial.print(F("/"));     Serial.println(N_NODES);

        /* Ред 2: статус на всеки node */
        const char* st_names[] = {
            "HEALTHY","WARM","THROTTLE","CRITICAL","DEAD"
        };
        for (int i = 0; i < N_NODES; i++) {
            Serial.print(F("N"));
            Serial.print(i);
            Serial.print(F(":"));
            Serial.print(st_names[nr.node_status[i]]);
            if (i < N_NODES - 1) Serial.print(F("|"));
        }
        Serial.println();

        step_count++;
    }

    delay(10);
}
