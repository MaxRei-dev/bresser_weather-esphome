#include "bresser_weather.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome
{
    namespace bresser_weather
    {
        static const char *const TAG = "bresser_weather";

        // ═════════════════════════════════════════════════════════════════════
        // setup()  –  läuft auf Core 1 (ESPHome-Loop)
        // ═════════════════════════════════════════════════════════════════════
        void BresserWeatherComponent::setup()
        {
            ESP_LOGI(TAG, "Setting up Bresser Weather Sensor Receiver");
            this->ws_.begin();

            // Queue anlegen: RawFrame-Structs von Core 0 → Core 1
            this->data_queue_ = xQueueCreate(RF_QUEUE_DEPTH, sizeof(RawFrame));
            if (this->data_queue_ == nullptr)
            {
                ESP_LOGE(TAG, "Queue konnte nicht erstellt werden!");
                return;
            }

            // RF-Task auf Core 0 starten
            BaseType_t result = xTaskCreatePinnedToCore(
                rf_task_,               // Task-Funktion
                "bresser_rf",           // Name (für Debugging)
                RF_TASK_STACK,          // Stack in Bytes
                this,                   // Argument: Zeiger auf Component
                RF_TASK_PRIORITY,       // Priorität
                &this->rf_task_handle_, // Handle
                RF_TASK_CORE            // Core (0)
            );

            if (result != pdPASS)
            {
                ESP_LOGE(TAG, "RF-Task konnte nicht gestartet werden! (err=%d)", result);
                return;
            }

#if BRESSER_DEBUG_STATS
            this->dbg_last_stats_ms_ = millis();
            ESP_LOGI(TAG, "RF-Task gestartet auf Core %d  Prio=%d  Queue=%d  Stats-Intervall=%ums",
                     RF_TASK_CORE, RF_TASK_PRIORITY, RF_QUEUE_DEPTH, BRESSER_STATS_INTERVAL_MS);
#else
            ESP_LOGI(TAG, "RF-Task gestartet auf Core %d  Prio=%d  Queue=%d  Stats=aus",
                     RF_TASK_CORE, RF_TASK_PRIORITY, RF_QUEUE_DEPTH);
#endif
        }

        // ═════════════════════════════════════════════════════════════════════
        // rf_task_()  –  läuft DAUERHAFT auf Core 0
        //
        // Darf NUR ws_-Zugriffe machen und in die Queue schreiben.
        // Kein publish_state(), kein ESP_LOG* mit großen Strings,
        // kein ESPHome-API-Zugriff – alles das ist nicht thread-sicher!
        // ═════════════════════════════════════════════════════════════════════
        void BresserWeatherComponent::rf_task_(void *arg)
        {
            BresserWeatherComponent *self = static_cast<BresserWeatherComponent *>(arg);

            for (;;)
            {
                int decode_status = self->ws_.getMessage();

                if (decode_status == DECODE_TIMEOUT)
                {
#if BRESSER_DEBUG_STATS
                    self->dbg_timeout_++;
#endif
                    // Kein Delay – sofort nächster Versuch.
                    // FreeRTOS-Scheduler gibt anderen Tasks trotzdem CPU.
                    continue;
                }

                if (decode_status != DECODE_OK)
                {
#if BRESSER_DEBUG_STATS
                    self->dbg_invalid_++;
#endif
                    continue;
                }

#if BRESSER_DEBUG_STATS
                self->dbg_ok_++;
#endif

                // Alle validen Slots in die Queue schreiben
                for (size_t i = 0; i < self->ws_.sensor.size(); ++i)
                {
                    if (!self->ws_.sensor[i].valid)
                        continue;

                    RawFrame frame{};
                    frame.s_type     = self->ws_.sensor[i].s_type;
                    frame.sensor_id  = self->ws_.sensor[i].sensor_id;
                    frame.rssi       = self->ws_.sensor[i].rssi;
                    frame.battery_ok = self->ws_.sensor[i].battery_ok;

                    // Weather-Union – gilt für WS und Pool
                    frame.temp_c              = self->ws_.sensor[i].w.temp_c;
                    frame.temp_ok             = self->ws_.sensor[i].w.temp_ok;
                    frame.humidity            = self->ws_.sensor[i].w.humidity;
                    frame.humidity_ok         = self->ws_.sensor[i].w.humidity_ok;
                    frame.wind_gust_meter_sec = self->ws_.sensor[i].w.wind_gust_meter_sec;
                    frame.wind_avg_meter_sec  = self->ws_.sensor[i].w.wind_avg_meter_sec;
                    frame.wind_direction_deg  = self->ws_.sensor[i].w.wind_direction_deg;
                    frame.wind_ok             = self->ws_.sensor[i].w.wind_ok;
                    frame.rain_mm             = self->ws_.sensor[i].w.rain_mm;
                    frame.rain_ok             = self->ws_.sensor[i].w.rain_ok;
                    frame.uv                  = self->ws_.sensor[i].w.uv;
                    frame.uv_ok               = self->ws_.sensor[i].w.uv_ok;
                    frame.light_klx           = self->ws_.sensor[i].w.light_klx;
                    frame.light_ok            = self->ws_.sensor[i].w.light_ok;

                    // Nicht blockierend senden (0 = kein Warten auf freien Platz).
                    // Bei vollem Queue wird der Frame verworfen – passiert nur wenn
                    // Core 1 für >RF_QUEUE_DEPTH Frames nicht zum Verarbeiten kommt.
                    if (xQueueSend(self->data_queue_, &frame, 0) != pdTRUE)
                    {
#if BRESSER_DEBUG_STATS
                        self->dbg_invalid_++; // Queue-Overflow als "verloren" zählen
#endif
                    }
                }

                self->ws_.clearSlots();
            }
            // Nie erreicht – Task läuft für immer
            vTaskDelete(nullptr);
        }

        // ═════════════════════════════════════════════════════════════════════
        // loop()  –  läuft auf Core 1 (ESPHome-Loop)
        //
        // Nur: Queue leeren + publish_state() aufrufen.
        // Kein ws_-Zugriff hier!
        // ═════════════════════════════════════════════════════════════════════
        void BresserWeatherComponent::loop()
        {
            if (this->data_queue_ == nullptr)
                return;

            // ── Debug-Stats (nur Core 1, kein Lock nötig für Log-Ausgabe) ─────
#if BRESSER_DEBUG_STATS
            {
                uint32_t now_ms = millis();
                if (now_ms - this->dbg_last_stats_ms_ >= BRESSER_STATS_INTERVAL_MS)
                {
                    uint32_t queue_waiting = uxQueueMessagesWaiting(this->data_queue_);
                    ESP_LOGI(TAG,
                             "[Stats] OK=%" PRIu32 "  Invalid=%" PRIu32
                             "  Timeout=%" PRIu32 "  Weather=%" PRIu32
                             "  Pool=%" PRIu32 "  Filtered=%" PRIu32
                             "  Unknown=%" PRIu32 "  Queue=%" PRIu32,
                             (uint32_t)this->dbg_ok_,
                             (uint32_t)this->dbg_invalid_,
                             (uint32_t)this->dbg_timeout_,
                             (uint32_t)this->dbg_weather_,
                             (uint32_t)this->dbg_pool_,
                             (uint32_t)this->dbg_filtered_,
                             (uint32_t)this->dbg_unknown_,
                             queue_waiting);
                    this->dbg_ok_       = 0;
                    this->dbg_invalid_  = 0;
                    this->dbg_timeout_  = 0;
                    this->dbg_weather_  = 0;
                    this->dbg_pool_     = 0;
                    this->dbg_filtered_ = 0;
                    this->dbg_unknown_  = 0;
                    this->dbg_last_stats_ms_ = now_ms;
                }
            }
#endif

            // ── Queue vollständig leeren ──────────────────────────────────────
            // Alle seit dem letzten loop()-Aufruf empfangenen Frames verarbeiten.
            RawFrame frame{};
            while (xQueueReceive(this->data_queue_, &frame, 0) == pdTRUE)
            {
                const uint8_t s_type = frame.s_type;

                // =============================================================
                // WETTERSTATION
                // =============================================================
                if (s_type == SENSOR_TYPE_WEATHER0 ||
                    s_type == SENSOR_TYPE_WEATHER1  ||
                    s_type == SENSOR_TYPE_WEATHER3  ||
                    s_type == SENSOR_TYPE_WEATHER8)
                {
                    if (this->filter_enabled_ && frame.sensor_id != this->filter_sensor_id_)
                    {
#if BRESSER_DEBUG_STATS
                        this->dbg_filtered_++;
#endif
                        ESP_LOGD(TAG, "[Weather] Filtered ID=%08X (want %08X)",
                                 (unsigned int)frame.sensor_id,
                                 (unsigned int)this->filter_sensor_id_);
                        continue;
                    }

#if BRESSER_DEBUG_STATS
                    this->dbg_weather_++;
#endif
                    char id_buf[16];
                    snprintf(id_buf, sizeof(id_buf), "%08X", (unsigned int)frame.sensor_id);

                    if (this->sensor_id_sensor_)
                        this->sensor_id_sensor_->publish_state(id_buf);
                    if (this->rssi_sensor_)
                        this->rssi_sensor_->publish_state(frame.rssi);
                    if (this->battery_sensor_)
                        this->battery_sensor_->publish_state(!frame.battery_ok);
                    if (frame.temp_ok && this->temperature_sensor_)
                        this->temperature_sensor_->publish_state(frame.temp_c);
                    if (frame.humidity_ok && this->humidity_sensor_)
                        this->humidity_sensor_->publish_state(frame.humidity);
                    if (frame.wind_ok)
                    {
                        if (this->wind_gust_sensor_)
                            this->wind_gust_sensor_->publish_state(frame.wind_gust_meter_sec);
                        if (this->wind_speed_sensor_)
                            this->wind_speed_sensor_->publish_state(frame.wind_avg_meter_sec);
                        if (this->wind_direction_sensor_)
                            this->wind_direction_sensor_->publish_state(frame.wind_direction_deg);
                    }
                    if (frame.rain_ok && this->rain_sensor_)
                        this->rain_sensor_->publish_state(frame.rain_mm);
                    if (frame.uv_ok && this->uv_sensor_)
                        this->uv_sensor_->publish_state(frame.uv);
                    if (frame.light_ok && this->light_sensor_)
                        this->light_sensor_->publish_state(frame.light_klx);

                    WeatherData data{};
                    data.sensor_id      = id_buf;
                    data.rssi           = frame.rssi;
                    data.battery_ok     = frame.battery_ok;
                    data.temperature    = frame.temp_ok     ? frame.temp_c              : NAN;
                    data.temperature_ok = frame.temp_ok;
                    data.humidity       = frame.humidity_ok ? (float)frame.humidity      : NAN;
                    data.humidity_ok    = frame.humidity_ok;
                    data.wind_gust      = frame.wind_ok     ? frame.wind_gust_meter_sec  : NAN;
                    data.wind_speed     = frame.wind_ok     ? frame.wind_avg_meter_sec   : NAN;
                    data.wind_direction = frame.wind_ok     ? frame.wind_direction_deg   : NAN;
                    data.wind_ok        = frame.wind_ok;
                    data.rain           = frame.rain_ok     ? frame.rain_mm              : NAN;
                    data.rain_ok        = frame.rain_ok;
                    data.uv             = frame.uv_ok       ? frame.uv                   : NAN;
                    data.uv_ok          = frame.uv_ok;
                    data.light          = frame.light_ok    ? frame.light_klx            : NAN;
                    data.light_ok       = frame.light_ok;
                    this->data_callback_.call(data);

                    ESP_LOGD(TAG,
                             "[Weather] ID=%s T=%.1f°C H=%d%% "
                             "Wnd=%.1f/%.1fm/s@%.0f° R=%.1fmm "
                             "UV=%.1f Light=%.1fklx RSSI=%.1fdBm Bat=%s",
                             id_buf, frame.temp_c, frame.humidity,
                             frame.wind_avg_meter_sec, frame.wind_gust_meter_sec,
                             frame.wind_direction_deg, frame.rain_mm,
                             frame.uv, frame.light_klx,
                             frame.rssi, frame.battery_ok ? "OK" : "Low");
                }

                // =============================================================
                // POOL THERMOMETER  (SENSOR_TYPE_POOL_THERMO = 3)
                // =============================================================
                else if (s_type == SENSOR_TYPE_POOL_THERMO)
                {
                    if (this->filter_pool_enabled_ && frame.sensor_id != this->filter_pool_sensor_id_)
                    {
#if BRESSER_DEBUG_STATS
                        this->dbg_filtered_++;
#endif
                        ESP_LOGD(TAG, "[Pool] Filtered ID=%08X (want %08X)",
                                 (unsigned int)frame.sensor_id,
                                 (unsigned int)this->filter_pool_sensor_id_);
                        continue;
                    }

#if BRESSER_DEBUG_STATS
                    this->dbg_pool_++;
#endif
                    char id_buf[16];
                    snprintf(id_buf, sizeof(id_buf), "%08X", (unsigned int)frame.sensor_id);

                    if (this->pool_sensor_id_sensor_)
                        this->pool_sensor_id_sensor_->publish_state(id_buf);
                    if (this->pool_rssi_sensor_)
                        this->pool_rssi_sensor_->publish_state(frame.rssi);
                    if (this->pool_battery_sensor_)
                        this->pool_battery_sensor_->publish_state(!frame.battery_ok);
                    if (frame.temp_ok && this->water_temperature_sensor_)
                        this->water_temperature_sensor_->publish_state(frame.temp_c);

                    WeatherData data{};
                    data.pool_valid           = true;
                    data.pool_sensor_id       = id_buf;
                    data.pool_rssi            = frame.rssi;
                    data.pool_battery_ok      = frame.battery_ok;
                    data.water_temperature    = frame.temp_ok ? frame.temp_c : NAN;
                    data.water_temperature_ok = frame.temp_ok;
                    data.temperature    = NAN; data.humidity = NAN;
                    data.wind_gust      = NAN; data.wind_speed = NAN;
                    data.wind_direction = NAN; data.rain = NAN;
                    data.uv             = NAN; data.light = NAN;
                    this->data_callback_.call(data);

                    ESP_LOGD(TAG, "[Pool] ID=%s WaterTemp=%.1f°C RSSI=%.1fdBm Bat=%s",
                             id_buf, frame.temp_c, frame.rssi,
                             frame.battery_ok ? "OK" : "Low");
                }

                // =============================================================
                // UNBEKANNTER TYP
                // =============================================================
                else
                {
#if BRESSER_DEBUG_STATS
                    this->dbg_unknown_++;
#endif
                    ESP_LOGV(TAG, "Unknown s_type=0x%02X ID=%08X RSSI=%.1fdBm",
                             s_type, (unsigned int)frame.sensor_id, frame.rssi);
                }
            } // while queue not empty
        } // loop()

    } // namespace bresser_weather
} // namespace esphome
