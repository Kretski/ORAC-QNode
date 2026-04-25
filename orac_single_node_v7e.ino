/**
 * ORAC-NT v7e — Single Node Guardian
 * ====================================
 * STM32F4 / Arduino compatible .ino
 * 
 * Свързване:
 *   DS18B20 DATA pin → PA0 (или друг аналогов пин)
 *   USB Serial       → управляващ компютър (115200 baud)
 *
 * Изход по Serial (всеки 100ms):
 *   STEP:42|T:67.3|W:0.412|U:0.087|E:0.531|STATUS:WARM
 *
 * Python wrapper чете този изход и управлява SpinQit.
 */

#include "orac_single_node_v7e.h"

/* ── КОНФИГУРАЦИЯ ─────────────────────────────────────── */
#define SERIAL_BAUD     115200
#define LOOP_MS         100       // Интервал на изход
#define TEMP_PIN        A0        // Аналогов пин за сензора

/* За реален DS18B20 — разкоментирай и добави библиотека:
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(PA0);
DallasTemperature sensors(&oneWire);
*/

/* ── ГЛОБАЛНО СЪСТОЯНИЕ ───────────────────────────────── */
ORAC_SingleState S;
uint32_t step_count = 0;
uint32_t last_print = 0;

/* ── ЧЕТЕНЕ НА ТЕМПЕРАТУРА ────────────────────────────── */
float read_temperature() {
    /* --- Реален DS18B20 ---
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C) return 25.0f;
    return t;
    */

    /* --- Симулация за тест ---
     * Бавно нарастваща температура с thermal spike на ~15 сек
     * Замени с реалния сензор при деплоймент
     */
    float t_ms = (float)millis() / 1000.0f;
    float base = 45.0f + 25.0f * sinf(t_ms / 20.0f);
    /* Spike около 15 сек */
    if (t_ms > 14.0f && t_ms < 17.0f) base += 30.0f;
    return base;
}

/* ── ИЗЧИСЛЯВАНЕ НА ТОВАР ─────────────────────────────── */
float compute_load(float T) {
    /* Нормализираме T към [0,1] като proxy за RF натоварване */
    float load = (T - 30.0f) / 60.0f;
    return (load < 0.0f) ? 0.0f : (load > 1.0f ? 1.0f : load);
}

/* ── SETUP ────────────────────────────────────────────── */
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1500);

    orac_single_init(&S);

    Serial.println(F("=== ORAC-NT v7e Single Node Guardian ==="));
    Serial.println(F("READY"));
    Serial.println(F("STEP|T|W|U|E|STATUS"));
}

/* ── MAIN LOOP ────────────────────────────────────────── */
void loop() {

    float T    = read_temperature();
    float load = compute_load(T);

    ORAC_Result r = orac_single_step(&S, T, load);

    /* Принтиране на всеки LOOP_MS */
    if (millis() - last_print >= LOOP_MS) {
        last_print = millis();

        /* Статусен стринг */
        const char* st;
        switch (r.status) {
            case ORAC_STATUS_HEALTHY:  st = "HEALTHY";  break;
            case ORAC_STATUS_WARM:     st = "WARM";     break;
            case ORAC_STATUS_THROTTLE: st = "THROTTLE"; break;
            case ORAC_STATUS_CRITICAL: st = "CRITICAL"; break;
            default:                   st = "DEAD";     break;
        }

        /* Компактен формат — Python го парсва лесно */
        Serial.print(step_count);   Serial.print(F("|"));
        Serial.print(T, 1);         Serial.print(F("|"));
        Serial.print(r.W, 4);       Serial.print(F("|"));
        Serial.print(r.U_t, 4);     Serial.print(F("|"));
        Serial.print(r.E_norm, 4);  Serial.print(F("|"));
        Serial.println(st);

        step_count++;
    }

    /* SEU / саможертва обработка */
    if (r.sacrificed) {
        Serial.println(F("!!! NODE SACRIFICED — HALTING !!!"));
        Serial.print(F("Final T=")); Serial.println(T, 1);
        while (1) delay(1000);
    }

    delay(10); /* Малка пауза за Serial буфера */
}
