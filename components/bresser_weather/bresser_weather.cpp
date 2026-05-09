#include "bresser_weather.h"
#include "esphome/core/log.h"

namespace esphome
{
    namespace bresser_weather
    {

        static const char *const TAG = "bresser_weather";

        void BresserWeatherComponent::setup()
        {
            ESP_LOGI(TAG, "Setting up Bresser Weather Sensor Receiver");
            this->ws_.begin();
            ESP_LOGI(TAG, "Receiver initialized successfully");
        }

        void BresserWeatherComponent::loop()
        {
            // Clear all sensor slots (resets valid-flags for the upcoming receive)
            this->ws_.clearSlots();

            // Try to receive one radio message (non-blocking).
            // Timeout occurs after a small multiple of expected time-on-air.
            int decode_status = this->ws_.getMessage();

            if (decode_status != DECODE_OK)
            {
                delay(100);
                return;
            }

            // -----------------------------------------------------------------------
            // Iterate all slots.  After a single getMessage() call exactly one slot
            // will be valid; the loop keeps the code robust for future library
            // changes and also handles the 6-in-1 two-message split correctly.
            // -----------------------------------------------------------------------
            for (size_t i = 0; i < this->ws_.sensor.size(); ++i)
            {
                if (!this->ws_.sensor[i].valid)
                    continue;

                const uint8_t s_type = this->ws_.sensor[i].s_type;

                // ===================================================================
                // WEATHER STATION  (5-in-1 / 6-in-1 / 7-in-1 / 8-in-1 / 3-in-1 WS)
                //
                // Library defines (WeatherSensor.h):
                //   SENSOR_TYPE_WEATHER0  = 0   5-in-1 station
                //   SENSOR_TYPE_WEATHER1  = 1   6-in-1 / 7-in-1 station
                //   SENSOR_TYPE_WEATHER3  = 12  3-in-1 Professional Rain Gauge
                //   SENSOR_TYPE_WEATHER8  = 13  8-in-1 station
                // ===================================================================
                if (s_type == SENSOR_TYPE_WEATHER0 ||
                    s_type == SENSOR_TYPE_WEATHER1  ||
                    s_type == SENSOR_TYPE_WEATHER3  ||
                    s_type == SENSOR_TYPE_WEATHER8)
                {
                    // Apply weather station sensor-ID filter
                    if (this->filter_enabled_ &&
                        this->ws_.sensor[i].sensor_id != this->filter_sensor_id_)
                    {
                        ESP_LOGD(TAG, "[Weather] Ignoring sensor ID %08X (filter: %08X)",
                                 (unsigned int)this->ws_.sensor[i].sensor_id,
                                 (unsigned int)this->filter_sensor_id_);
                        continue;
                    }

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

                    // Build WeatherData and fire callbacks
                    WeatherData data{};
                    data.sensor_id      = id_buf;
                    data.rssi           = this->ws_.sensor[i].rssi;
                    data.battery_ok     = this->ws_.sensor[i].battery_ok;
                    data.temperature    = this->ws_.sensor[i].w.temp_ok      ? this->ws_.sensor[i].w.temp_c             : NAN;
                    data.temperature_ok = this->ws_.sensor[i].w.temp_ok;
                    data.humidity       = this->ws_.sensor[i].w.humidity_ok  ? (float)this->ws_.sensor[i].w.humidity    : NAN;
                    data.humidity_ok    = this->ws_.sensor[i].w.humidity_ok;
                    data.wind_gust      = this->ws_.sensor[i].w.wind_ok      ? this->ws_.sensor[i].w.wind_gust_meter_sec : NAN;
                    data.wind_speed     = this->ws_.sensor[i].w.wind_ok      ? this->ws_.sensor[i].w.wind_avg_meter_sec  : NAN;
                    data.wind_direction = this->ws_.sensor[i].w.wind_ok      ? this->ws_.sensor[i].w.wind_direction_deg  : NAN;
                    data.wind_ok        = this->ws_.sensor[i].w.wind_ok;
                    data.rain           = this->ws_.sensor[i].w.rain_ok      ? this->ws_.sensor[i].w.rain_mm             : NAN;
                    data.rain_ok        = this->ws_.sensor[i].w.rain_ok;
                    data.uv             = this->ws_.sensor[i].w.uv_ok        ? this->ws_.sensor[i].w.uv                  : NAN;
                    data.uv_ok          = this->ws_.sensor[i].w.uv_ok;
                    data.light          = this->ws_.sensor[i].w.light_ok     ? this->ws_.sensor[i].w.light_klx           : NAN;
                    data.light_ok       = this->ws_.sensor[i].w.light_ok;
                    // pool_valid stays false; pool fields stay at their NAN/false defaults
                    this->data_callback_.call(data);

                    ESP_LOGD(TAG,
                             "[Weather] ID=%s T=%.1f°C H=%d%% Wnd=%.1f/%.1f@%.0f° "
                             "R=%.1fmm UV=%.1f Lux=%.1fklx RSSI=%.1f Bat=%s",
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

                // ===================================================================
                // POOL / SPA THERMOMETER  (6-in-1 decoder, PN 7000073)
                //
                // Library define: SENSOR_TYPE_POOL_THERMO = 3
                // Data is in the Weather union: w.temp_c / w.temp_ok
                // ===================================================================
                else if (s_type == SENSOR_TYPE_POOL_THERMO)
                {
                    // Apply pool sensor-ID filter
                    if (this->filter_pool_enabled_ &&
                        this->ws_.sensor[i].sensor_id != this->filter_pool_sensor_id_)
                    {
                        ESP_LOGD(TAG, "[Pool] Ignoring sensor ID %08X (filter: %08X)",
                                 (unsigned int)this->ws_.sensor[i].sensor_id,
                                 (unsigned int)this->filter_pool_sensor_id_);
                        continue;
                    }

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

                    // Pool temperature lives in w.temp_c (Weather union, same as WS)
                    if (this->ws_.sensor[i].w.temp_ok && this->water_temperature_sensor_)
                        this->water_temperature_sensor_->publish_state(
                            this->ws_.sensor[i].w.temp_c);

                    // Build WeatherData and fire callbacks
                    WeatherData data{};
                    data.pool_valid           = true;
                    data.pool_sensor_id       = id_buf;
                    data.pool_rssi            = this->ws_.sensor[i].rssi;
                    data.pool_battery_ok      = this->ws_.sensor[i].battery_ok;
                    data.water_temperature    = this->ws_.sensor[i].w.temp_ok
                                                    ? this->ws_.sensor[i].w.temp_c : NAN;
                    data.water_temperature_ok = this->ws_.sensor[i].w.temp_ok;
                    // Weather fields stay at NAN / false defaults
                    data.temperature    = NAN;
                    data.humidity       = NAN;
                    data.wind_gust      = NAN;
                    data.wind_speed     = NAN;
                    data.wind_direction = NAN;
                    data.rain           = NAN;
                    data.uv             = NAN;
                    data.light          = NAN;
                    this->data_callback_.call(data);

                    ESP_LOGD(TAG, "[Pool] ID=%s WaterTemp=%.1f°C RSSI=%.1f Bat=%s",
                             id_buf,
                             this->ws_.sensor[i].w.temp_c,
                             this->ws_.sensor[i].rssi,
                             this->ws_.sensor[i].battery_ok ? "OK" : "Low");
                }
                else
                {
                    ESP_LOGD(TAG, "Slot %zu: unhandled s_type=0x%02X sensor_id=%08X",
                             i, s_type, (unsigned int)this->ws_.sensor[i].sensor_id);
                }
            } // for each slot

            delay(100);
        }

    } // namespace bresser_weather
} // namespace esphome
