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

// Safety fallback: SENSOR_TYPE_POOL_THERMO was added to WeatherSensor.h on
// 20231024. If an older library copy is installed the define would be missing.
#ifndef SENSOR_TYPE_POOL_THERMO
#define SENSOR_TYPE_POOL_THERMO 3
#endif

namespace esphome
{
    namespace bresser_weather
    {
        struct WeatherData
        {
            // ---- Weather station fields ----
            std::string sensor_id;
            float rssi;
            bool battery_ok;
            float temperature;
            bool temperature_ok;
            float humidity;
            bool humidity_ok;
            float wind_gust;
            float wind_speed;
            float wind_direction;
            bool wind_ok;
            float rain;
            bool rain_ok;
            float uv;
            bool uv_ok;
            float light;
            bool light_ok;

            // ---- Pool / Spa Thermometer (3-in-1, PN 7000073) ----
            // pool_valid is true only in frames that carry pool data.
            // In those frames the weather fields above are set to NAN / false.
            bool pool_valid{false};
            std::string pool_sensor_id;
            float pool_rssi{NAN};
            bool pool_battery_ok{false};
            float water_temperature{NAN};
            bool water_temperature_ok{false};
        };

        class BresserWeatherComponent : public Component
        {
        public:
            void setup() override;
            void loop() override;
            float get_setup_priority() const override { return setup_priority::DATA; }

            // ---- Weather station sensors ----
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

            // ---- Pool thermometer sensors ----
            void set_water_temperature_sensor(sensor::Sensor *s)            { water_temperature_sensor_ = s; }
            void set_pool_rssi_sensor(sensor::Sensor *s)                     { pool_rssi_sensor_ = s; }
            void set_pool_battery_sensor(binary_sensor::BinarySensor *s)    { pool_battery_sensor_ = s; }
            void set_pool_sensor_id_text_sensor(text_sensor::TextSensor *s) { pool_sensor_id_sensor_ = s; }

            // ---- Filters ----
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
            WeatherSensor ws_;

            // ---- Weather station filter ----
            uint32_t filter_sensor_id_{0};
            bool     filter_enabled_{false};

            // ---- Pool sensor filter ----
            uint32_t filter_pool_sensor_id_{0};
            bool     filter_pool_enabled_{false};

            // ---- Weather station sensor pointers ----
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

            // ---- Pool thermometer sensor pointers ----
            sensor::Sensor *water_temperature_sensor_{nullptr};
            sensor::Sensor *pool_rssi_sensor_{nullptr};
            binary_sensor::BinarySensor *pool_battery_sensor_{nullptr};
            text_sensor::TextSensor     *pool_sensor_id_sensor_{nullptr};

            CallbackManager<void(const WeatherData &)> data_callback_;
        };

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
