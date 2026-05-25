#pragma once
#include <cmath>
#include <string>
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "WeatherSensorCfg.h"
#include "WeatherSensor.h"

// FreeRTOS – nur auf ESP32 verfügbar
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Safety fallbacks
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SENSOR_TYPE_POOL_THERMO
#define SENSOR_TYPE_POOL_THERMO 3
#endif

#ifndef DECODE_TIMEOUT
#define DECODE_TIMEOUT DECODE_INVALID
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Debug / Frame-Loss Statistiken
//
// Deaktivieren:  build_flags: ["-DBRESSER_DEBUG_STATS=0"]
// Intervall:     build_flags: ["-DBRESSER_STATS_INTERVAL_MS=30000"]
// ─────────────────────────────────────────────────────────────────────────────
#ifndef BRESSER_DEBUG_STATS
#define BRESSER_DEBUG_STATS 1
#endif

#if BRESSER_DEBUG_STATS
#ifndef BRESSER_STATS_INTERVAL_MS
#define BRESSER_STATS_INTERVAL_MS 60000U
#endif
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Dual-Core Konfiguration
//
// RF_TASK_CORE:       CPU-Core für den RF-Empfangs-Task (0 oder 1)
// RF_TASK_PRIORITY:   FreeRTOS-Priorität (1 = niedrig, 24 = WiFi-Stack)
// RF_TASK_STACK:      Stack-Größe in Bytes
// RF_QUEUE_DEPTH:     Maximale gepufferte Frames zwischen den Cores
// ─────────────────────────────────────────────────────────────────────────────
#ifndef RF_TASK_CORE
#define RF_TASK_CORE     0          // Core 0: teilt sich mit WiFi-Stack
#endif
#ifndef RF_TASK_PRIORITY
#define RF_TASK_PRIORITY 2          // Unter WiFi (23), über Idle (0)
#endif
#ifndef RF_TASK_STACK
#define RF_TASK_STACK    8192       // Bytes – RadioLib + Frame-Decoding braucht >4096
#endif
#ifndef RF_QUEUE_DEPTH
#define RF_QUEUE_DEPTH   8          // Frames die gepuffert werden können
#endif

namespace esphome
{
    namespace bresser_weather
    {
        // ─────────────────────────────────────────────────────────────────────
        // WeatherData – wird über den on_value Callback an Automationen
        // übergeben. Lebt ausschließlich auf Core 1 (ESPHome-Loop).
        // ─────────────────────────────────────────────────────────────────────
        struct WeatherData
        {
            // ── Wetterstation ────────────────────────────────────────────────
            std::string sensor_id;
            float rssi;
            bool battery_ok;
            float temperature;      bool temperature_ok;
            float humidity;         bool humidity_ok;
            float wind_gust;
            float wind_speed;
            float wind_direction;   bool wind_ok;
            float rain;             bool rain_ok;
            float uv;               bool uv_ok;
            float light;            bool light_ok;

            // ── Pool / Spa Thermometer (PN 7000073) ──────────────────────────
            bool pool_valid{false};
            std::string pool_sensor_id;
            float pool_rssi{NAN};
            bool pool_battery_ok{false};
            float water_temperature{NAN};
            bool water_temperature_ok{false};
        };

        // ─────────────────────────────────────────────────────────────────────
        // RawFrame – POD-Struct ohne dynamische Allokation.
        // Wird über die FreeRTOS-Queue von Core 0 → Core 1 übertragen.
        // Kein std::string, keine Zeiger – queue-sicher.
        // ─────────────────────────────────────────────────────────────────────
        struct RawFrame
        {
            uint8_t  s_type;
            uint32_t sensor_id;
            float    rssi;
            bool     battery_ok;

            // Weather-Union Felder (auch für Pool genutzt)
            float    temp_c;                bool temp_ok;
            uint8_t  humidity;              bool humidity_ok;
            float    wind_gust_meter_sec;
            float    wind_avg_meter_sec;
            float    wind_direction_deg;    bool wind_ok;
            float    rain_mm;               bool rain_ok;
            float    uv;                    bool uv_ok;
            float    light_klx;             bool light_ok;
        };

        // ─────────────────────────────────────────────────────────────────────
        class BresserWeatherComponent : public Component
        {
        public:
            void setup() override;
            void loop() override;
            float get_setup_priority() const override { return setup_priority::DATA; }

            // ── Wetterstation Sensor-Setter ───────────────────────────────────
            void set_temperature_sensor(sensor::Sensor *s)             { temperature_sensor_ = s; }
            void set_humidity_sensor(sensor::Sensor *s)                { humidity_sensor_ = s; }
            void set_wind_gust_sensor(sensor::Sensor *s)               { wind_gust_sensor_ = s; }
            void set_wind_speed_sensor(sensor::Sensor *s)              { wind_speed_sensor_ = s; }
            void set_wind_direction_sensor(sensor::Sensor *s)          { wind_direction_sensor_ = s; }
            void set_rain_sensor(sensor::Sensor *s)                    { rain_sensor_ = s; }
            void set_uv_sensor(sensor::Sensor *s)                      { uv_sensor_ = s; }
            void set_light_sensor(sensor::Sensor *s)                   { light_sensor_ = s; }
            void set_rssi_sensor(sensor::Sensor *s)                    { rssi_sensor_ = s; }
            void set_battery_sensor(binary_sensor::BinarySensor *s)    { battery_sensor_ = s; }
            void set_sensor_id_text_sensor(text_sensor::TextSensor *s) { sensor_id_sensor_ = s; }

            // ── Pool Thermometer Sensor-Setter ────────────────────────────────
            void set_water_temperature_sensor(sensor::Sensor *s)            { water_temperature_sensor_ = s; }
            void set_pool_rssi_sensor(sensor::Sensor *s)                     { pool_rssi_sensor_ = s; }
            void set_pool_battery_sensor(binary_sensor::BinarySensor *s)    { pool_battery_sensor_ = s; }
            void set_pool_sensor_id_text_sensor(text_sensor::TextSensor *s) { pool_sensor_id_sensor_ = s; }

            // ── Filter ────────────────────────────────────────────────────────
            void set_filter_sensor_id(uint32_t id)
            {
                filter_sensor_id_ = id;
                filter_enabled_   = true;
            }
            void set_filter_pool_sensor_id(uint32_t id)
            {
                filter_pool_sensor_id_ = id;
                filter_pool_enabled_   = true;
            }

            void add_on_value_callback(std::function<void(const WeatherData &)> cb)
            {
                this->data_callback_.add(std::move(cb));
            }

        protected:
            // ── Radio (nur von rf_task_ auf Core 0 anfassen!) ─────────────────
            WeatherSensor ws_;

            // ── Filter ────────────────────────────────────────────────────────
            uint32_t filter_sensor_id_{0};      bool filter_enabled_{false};
            uint32_t filter_pool_sensor_id_{0}; bool filter_pool_enabled_{false};

            // ── Sensor-Zeiger (ESPHome Core 1) ───────────────────────────────
            sensor::Sensor *temperature_sensor_{nullptr};
            sensor::Sensor *humidity_sensor_{nullptr};
            sensor::Sensor *wind_gust_sensor_{nullptr};
            sensor::Sensor *wind_speed_sensor_{nullptr};
            sensor::Sensor *wind_direction_sensor_{nullptr};
            sensor::Sensor *rain_sensor_{nullptr};
            sensor::Sensor *uv_sensor_{nullptr};
            sensor::Sensor *light_sensor_{nullptr};
            sensor::Sensor *rssi_sensor_{nullptr};
            binary_sensor::BinarySensor *battery_sensor_{nullptr};
            text_sensor::TextSensor     *sensor_id_sensor_{nullptr};

            sensor::Sensor *water_temperature_sensor_{nullptr};
            sensor::Sensor *pool_rssi_sensor_{nullptr};
            binary_sensor::BinarySensor *pool_battery_sensor_{nullptr};
            text_sensor::TextSensor     *pool_sensor_id_sensor_{nullptr};

            // ── FreeRTOS – Dual-Core ──────────────────────────────────────────
            // Queue: Core 0 (RF-Task) schreibt RawFrames,
            //        Core 1 (ESPHome loop) liest und publiziert.
            QueueHandle_t data_queue_{nullptr};
            TaskHandle_t  rf_task_handle_{nullptr};

            // Statische Task-Funktion (FreeRTOS braucht C-Linkage-kompatible Signatur)
            static void rf_task_(void *arg);

            // ── Debug-Zähler (volatile: von zwei Cores gelesen/geschrieben) ───
#if BRESSER_DEBUG_STATS
            volatile uint32_t dbg_ok_{0};
            volatile uint32_t dbg_invalid_{0};
            volatile uint32_t dbg_timeout_{0};
            volatile uint32_t dbg_weather_{0};
            volatile uint32_t dbg_pool_{0};
            volatile uint32_t dbg_filtered_{0};
            volatile uint32_t dbg_unknown_{0};
            uint32_t dbg_last_stats_ms_{0};   // nur Core 1
#endif

            CallbackManager<void(const WeatherData &)> data_callback_;
        };

        // ─────────────────────────────────────────────────────────────────────
        class WeatherDataTrigger : public Trigger<WeatherData>
        {
        public:
            explicit WeatherDataTrigger(BresserWeatherComponent *parent)
            {
                parent->add_on_value_callback([this](const WeatherData &data)
                                              { this->trigger(data); });
            }
        };
    } // namespace bresser_weather
} // namespace esphome
