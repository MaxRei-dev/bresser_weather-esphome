#include "bresser_weather.h"
#include "esphome/core/log.h"

namespace esphome
{
    namespace bresser_weather
    {

        static const char *const TAG = "bresser_weather";

        // ─────────────────────────────────────────────────────────────────────
        void BresserWeatherComponent::setup()
        {
            ESP_LOGI(TAG, "Setting up Bresser Weather Sensor Receiver");
            this->ws_.begin();
#if BRESSER_DEBUG_STATS
            this->dbg_last_stats_ms_ = millis();
            ESP_LOGI(TAG, "Frame-loss statistics enabled (interval %u ms)",
                     BRESSER_STATS_INTERVAL_MS);
#else
            ESP_LOGI(TAG, "Frame-loss statistics disabled (BRESSER_DEBUG_STATS=0)");
#endif
            ESP_LOGI(TAG, "Receiver initialized successfully");
        }

        // ─────────────────────────────────────────────────────────────────────
        void BresserWeatherComponent::loop()
        {
            // ── Periodic stats summary ────────────────────────────────────────
            // Compiled out completely when BRESSER_DEBUG_STATS=0.
#if BRESSER_DEBUG_STATS
            {
                uint32_t now_ms = millis();
                if (now_ms - this->dbg_last_stats_ms_ >= BRESSER_STATS_INTERVAL_MS)
                {
                    ESP_LOGI(TAG,
                             "[Stats] OK=%" PRIu32 "  Invalid=%" PRIu32
                             "  Timeout=%" PRIu32 "  Weather=%" PRIu32
                             "  Pool=%" PRIu32 "  Filtered=%" PRIu32
                             "  Unknown=%" PRIu32,
                             this->dbg_ok_,
                             this->dbg_invalid_,
                             this->dbg_timeout_,
                             this->dbg_weather_,
                             this->dbg_pool_,
                             this->dbg_filtered_,
                             this->dbg_unknown_);
                    this->dbg_ok_            = 0;
                    this->dbg_invalid_       = 0;
                    this->dbg_timeout_       = 0;
                    this->dbg_weather_       = 0;
                    this->dbg_pool_          = 0;
                    this->dbg_filtered_      = 0;
                    this->dbg_unknown_       = 0;
                    this->dbg_last_stats_ms_ = now_ms;
                }
            }
#endif // BRESSER_DEBUG_STATS

            // ── Receive one radio frame ───────────────────────────────────────
            // NOTE: clearSlots() is called AFTER processing, not before
            // getMessage(). Calling it at the top of the loop would discard
            // any frame the library has already buffered before we read it.
            int decode_status = this->ws_.getMessage();

            if (decode_status == DECODE_TIMEOUT)
            {
#if BRESSER_DEBUG_STATS
                this->dbg_timeout_++;
#endif
                // No frame in receive window – yield so other components get CPU.
                // Never use delay() here; it blocks the entire ESPHome loop.
                yield();
                return;
            }

            if (decode_status != DECODE_OK)
            {
                // DECODE_INVALID: frame received but CRC/decode failed.
                // High rate = RF interference or a nearby 868 MHz device.
#if BRESSER_DEBUG_STATS
                this->dbg_invalid_++;
                ESP_LOGV(TAG, "DECODE_INVALID (total=%" PRIu32 ")", this->dbg_invalid_);
#else
                ESP_LOGV(TAG, "DECODE_INVALID");
#endif
                yield();
                return;
            }

#if BRESSER_DEBUG_STATS
            this->dbg_ok_++;
#endif

            // ── Process all valid slots ───────────────────────────────────────
            // After a single getMessage() exactly one slot is valid.
            // Iterating all slots keeps the code correct for future library
            // changes and 6-in-1 two-message sequences.
            for (size_t i = 0; i < this->ws_.sensor.size(); ++i)
            {
                if (!this->ws_.sensor[i].valid)
                    continue;

                const uint8_t s_type = this->ws_.sensor[i].s_type;

                // =================================================================
                // WEATHER STATION  (5-in-1 / 6-in-1 / 7-in-1 / 8-in-1 / rain WS)
                //
                // Library defines (WeatherSensor.h):
                //   SENSOR_TYPE_WEATHER0 = 0   5-in-1
                //   SENSOR_TYPE_WEATHER1 = 1   6-in-1 / 7-in-1
                //   SENSOR_TYPE_WEATHER3 = 12  3-in-1 Professional Rain Gauge
                //   SENSOR_TYPE_WEATHER8 = 13  8-in-1
                // =================================================================
                if (s_type == SENSOR_TYPE_WEATHER0 ||
                    s_type == SENSOR_TYPE_WEATHER1  ||
                    s_type == SENSOR_TYPE_WEATHER3  ||
                    s_type == SENSOR_TYPE_WEATHER8)
                {
                    if (this->filter_enabled_ &&
                        this->ws_.sensor[i].sensor_id != this->filter_sensor_id_)
                    {
#if BRESSER_DEBUG_STATS
                        this->dbg_filtered_++;
#endif
                        ESP_LOGD(TAG, "[Weather] Filtered s_type=0x%02X ID=%08X (want %08X)",
                                 s_type,
                                 (unsigned int)this->ws_.sensor[i].sensor_id,
                                 (unsigned int)this->filter_sensor_id_);
                        continue;
                    }

#if BRESSER_DEBUG_STATS
                    this->dbg_weather_++;
#endif
                    char id_buf[16];
                    snprintf(id_buf, sizeof(id_buf), "%08X",
                             (unsigned int)this->ws_.sensor[i].sensor_id);

                    if (this->sensor_id_sensor_)
                        this->sensor_id_sensor_->publish_state(id_buf);

                    if (this->rssi_sensor_)
                        this->rssi_sensor_->publish_state(this->ws_.sensor[i].rssi);

                    // HA BATTERY device_class: ON = Low, OFF = OK  →  invert
                    if (this->battery_sensor_)
                        this->battery_sensor_->publish_state(!this->ws_.sensor[i].battery_ok);

                    if (this->ws_.sensor[i].w.temp_ok && this->temperature_sensor_)
                        this->temperature_sensor_->publish_state(this->ws_.sensor[i].w.temp_c);

                    if (this->ws_.sensor[i].w.humidity_ok && this->humidity_sensor_)
                        this->humidity_sensor_->publish_state(this->ws_.sensor[i].w.humidity);

                    if (this->ws_.sensor[i].w.wind_ok)
                    {
                        if (this->wind_gust_sensor_)
                            this->wind_gust_sensor_->publish_state(
                                this->ws_.sensor[i].w.wind_gust_meter_sec);
                        if (this->wind_speed_sensor_)
                            this->wind_speed_sensor_->publish_state(
                                this->ws_.sensor[i].w.wind_avg_meter_sec);
                        if (this->wind_direction_sensor_)
                            this->wind_direction_sensor_->publish_state(
                                this->ws_.sensor[i].w.wind_direction_deg);
                    }

                    if (this->ws_.sensor[i].w.rain_ok && this->rain_sensor_)
                        this->rain_sensor_->publish_state(this->ws_.sensor[i].w.rain_mm);

                    if (this->ws_.sensor[i].w.uv_ok && this->uv_sensor_)
                        this->uv_sensor_->publish_state(this->ws_.sensor[i].w.uv);

                    if (this->ws_.sensor[i].w.light_ok && this->light_sensor_)
                        this->light_sensor_->publish_state(this->ws_.sensor[i].w.light_klx);

                    WeatherData data{};
                    data.sensor_id      = id_buf;
                    data.rssi           = this->ws_.sensor[i].rssi;
                    data.battery_ok     = this->ws_.sensor[i].battery_ok;
                    data.temperature    = this->ws_.sensor[i].w.temp_ok     ? this->ws_.sensor[i].w.temp_c              : NAN;
                    data.temperature_ok = this->ws_.sensor[i].w.temp_ok;
                    data.humidity       = this->ws_.sensor[i].w.humidity_ok ? (float)this->ws_.sensor[i].w.humidity     : NAN;
                    data.humidity_ok    = this->ws_.sensor[i].w.humidity_ok;
                    data.wind_gust      = this->ws_.sensor[i].w.wind_ok     ? this->ws_.sensor[i].w.wind_gust_meter_sec : NAN;
                    data.wind_speed     = this->ws_.sensor[i].w.wind_ok     ? this->ws_.sensor[i].w.wind_avg_meter_sec  : NAN;
                    data.wind_direction = this->ws_.sensor[i].w.wind_ok     ? this->ws_.sensor[i].w.wind_direction_deg  : NAN;
                    data.wind_ok        = this->ws_.sensor[i].w.wind_ok;
                    data.rain           = this->ws_.sensor[i].w.rain_ok     ? this->ws_.sensor[i].w.rain_mm             : NAN;
                    data.rain_ok        = this->ws_.sensor[i].w.rain_ok;
                    data.uv             = this->ws_.sensor[i].w.uv_ok       ? this->ws_.sensor[i].w.uv                  : NAN;
                    data.uv_ok          = this->ws_.sensor[i].w.uv_ok;
                    data.light          = this->ws_.sensor[i].w.light_ok    ? this->ws_.sensor[i].w.light_klx           : NAN;
                    data.light_ok       = this->ws_.sensor[i].w.light_ok;
                    // pool_valid stays false; pool fields keep NAN/false defaults
                    this->data_callback_.call(data);

                    ESP_LOGD(TAG,
                             "[Weather] ID=%s T=%.1f°C H=%d%% "
                             "Wnd=%.1f/%.1fm/s@%.0f° R=%.1fmm "
                             "UV=%.1f Light=%.1fklx RSSI=%.1fdBm Bat=%s",
                             id_buf,
                             this->ws_.sensor[i].w.temp_c,
                             this->ws_.sensor[i].w.humidity,
                             this->ws_.sensor[i].w.wind_avg_meter_sec,
                             this->ws_.sensor[i].w.wind_gust_meter_sec,
                             this->ws_.sensor[i].w.wind_direction_deg,
                             this->ws_.sensor[i].w.rain_mm,
                             this->ws_.sensor[i].w.uv,
                             this->ws_.sensor[i].w.light_klx,
                             this->ws_.sensor[i].rssi,
                             this->ws_.sensor[i].battery_ok ? "OK" : "Low");
                }

                // =================================================================
                // POOL / SPA THERMOMETER  (6-in-1 decoder, PN 7000073)
                //
                // Library define: SENSOR_TYPE_POOL_THERMO = 3
                // Temperature in Weather union: w.temp_c / w.temp_ok
                // =================================================================
                else if (s_type == SENSOR_TYPE_POOL_THERMO)
                {
                    if (this->filter_pool_enabled_ &&
                        this->ws_.sensor[i].sensor_id != this->filter_pool_sensor_id_)
                    {
#if BRESSER_DEBUG_STATS
                        this->dbg_filtered_++;
#endif
                        ESP_LOGD(TAG, "[Pool] Filtered ID=%08X (want %08X)",
                                 (unsigned int)this->ws_.sensor[i].sensor_id,
                                 (unsigned int)this->filter_pool_sensor_id_);
                        continue;
                    }

#if BRESSER_DEBUG_STATS
                    this->dbg_pool_++;
#endif
                    char id_buf[16];
                    snprintf(id_buf, sizeof(id_buf), "%08X",
                             (unsigned int)this->ws_.sensor[i].sensor_id);

                    if (this->pool_sensor_id_sensor_)
                        this->pool_sensor_id_sensor_->publish_state(id_buf);

                    if (this->pool_rssi_sensor_)
                        this->pool_rssi_sensor_->publish_state(this->ws_.sensor[i].rssi);

                    // HA BATTERY device_class: ON = Low, OFF = OK  →  invert
                    if (this->pool_battery_sensor_)
                        this->pool_battery_sensor_->publish_state(!this->ws_.sensor[i].battery_ok);

                    // Pool temperature is in w.temp_c (same Weather union as WS)
                    if (this->ws_.sensor[i].w.temp_ok && this->water_temperature_sensor_)
                        this->water_temperature_sensor_->publish_state(
                            this->ws_.sensor[i].w.temp_c);

                    WeatherData data{};
                    data.pool_valid           = true;
                    data.pool_sensor_id       = id_buf;
                    data.pool_rssi            = this->ws_.sensor[i].rssi;
                    data.pool_battery_ok      = this->ws_.sensor[i].battery_ok;
                    data.water_temperature    = this->ws_.sensor[i].w.temp_ok
                                                    ? this->ws_.sensor[i].w.temp_c : NAN;
                    data.water_temperature_ok = this->ws_.sensor[i].w.temp_ok;
                    data.temperature    = NAN;
                    data.humidity       = NAN;
                    data.wind_gust      = NAN;
                    data.wind_speed     = NAN;
                    data.wind_direction = NAN;
                    data.rain           = NAN;
                    data.uv             = NAN;
                    data.light          = NAN;
                    this->data_callback_.call(data);

                    ESP_LOGD(TAG, "[Pool] ID=%s WaterTemp=%.1f°C RSSI=%.1fdBm Bat=%s",
                             id_buf,
                             this->ws_.sensor[i].w.temp_c,
                             this->ws_.sensor[i].rssi,
                             this->ws_.sensor[i].battery_ok ? "OK" : "Low");
                }

                // =================================================================
                // UNKNOWN – VERBOSE so it doesn't spam at normal log levels
                // =================================================================
                else
                {
#if BRESSER_DEBUG_STATS
                    this->dbg_unknown_++;
                    ESP_LOGV(TAG,
                             "Unknown s_type=0x%02X ID=%08X RSSI=%.1fdBm "
                             "(total unknown=%" PRIu32 ")",
                             s_type,
                             (unsigned int)this->ws_.sensor[i].sensor_id,
                             this->ws_.sensor[i].rssi,
                             this->dbg_unknown_);
#else
                    ESP_LOGV(TAG, "Unknown s_type=0x%02X ID=%08X RSSI=%.1fdBm",
                             s_type,
                             (unsigned int)this->ws_.sensor[i].sensor_id,
                             this->ws_.sensor[i].rssi);
#endif
                }
            } // for each slot

            // Clear slots AFTER processing – never before getMessage(),
            // or buffered frames would be thrown away before being read.
            this->ws_.clearSlots();

        } // loop()

    } // namespace bresser_weather
} // namespace esphome
