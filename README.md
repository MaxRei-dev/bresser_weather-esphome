# ESPHome Bresser Weather Sensor Component

ESPHome component for Bresser weather sensors and the Bresser Pool / Spa Thermometer (PN 7000073) using a CC1101 868 MHz radio module.

This project is made possible by the excellent [BresserWeatherSensorReceiver](https://github.com/matthias-bs/BresserWeatherSensorReceiver/tree/main) library by Matthias Prinke. All credit for sensor decoding and radio communication belongs to that project.

> **Disclaimer**: Developed with AI assistance, tested with the hardware configurations listed below.

## Tested Hardware

| Microcontroller | Radio Module   | Sensor                                      |
| --------------- | -------------- | ------------------------------------------- |
| ESP32           | CC1101 868 MHz | Bresser 7-in-1 (7003100)                    |
| ESP32           | CC1101 868 MHz | Bresser 7-in-1 + Pool Thermometer (7000073) |

## Features

- Direct Home Assistant integration via ESPHome API
- Weather station: Temperature, Humidity, Wind (speed / gust / direction), Rain, UV, Light
- Pool / Spa Thermometer support (Bresser PN 7000073)
- Per-sensor ID filtering (`filter_sensor_id`, `filter_pool_sensor_id`)
- `on_value` automation trigger with full `WeatherData` struct
- Non-blocking loop — compatible with all other ESPHome components

## Wiring (ESP32 + CC1101)

| CC1101 | ESP32 | GPIO |
| ------ | ----- | ---- |
| VCC    | 3.3V  | —    |
| GND    | GND   | —    |
| SCLK   | —     | 18   |
| MISO   | —     | 19   |
| MOSI   | —     | 23   |
| CS     | —     | 27   |
| GD0    | —     | 21   |
| GD2    | —     | 33   |

> **Important**: CC1101 requires **3.3V** — not 5V.

## Installation

```yaml
external_components:
  - source: github://MaxRei-dev/bresser_weather-esphome@main
    refresh: 0s
    components: [bresser_weather]
```

## Configuration

### Required options

| Key         | Description                                    |
| ----------- | ---------------------------------------------- |
| `radio`     | Radio chip — use `cc1101`                      |
| `pins.cs`   | SPI chip select GPIO                           |
| `pins.irq`  | Interrupt / GD0 GPIO                           |
| `pins.gpio` | GD2 GPIO                                       |
| `pins.rst`  | Reset GPIO — use `-1` if not connected         |

### Full example

```yaml
substitutions:
  friendly_name: "Weather Station"

esphome:
  name: wetterstation
  friendly_name: ${friendly_name}
  libraries:
    - SPI

esp32:
  board: esp32dev
  framework:
    type: arduino
    version: 3.3.6
    advanced:
      minimum_chip_revision: "3.0"

spi:
  clk_pin: GPIO18
  miso_pin: GPIO19
  mosi_pin: GPIO23

external_components:
  - source: github://MaxRei-dev/bresser_weather-esphome@main
    components: [bresser_weather]

logger:

web_server:
  port: 80

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

sensor:
  - platform: internal_temperature
    name: "${friendly_name} Internal Temperature"

button:
  - platform: restart
    name: "${friendly_name} Restart"

bresser_weather:
  radio: cc1101
  pins:
    cs: 27
    irq: 21
    gpio: 33
    rst: -1
  filter_sensor_id: 0x00008BFC       # replace with your weather station ID
  filter_pool_sensor_id: 0x27761309  # replace with your pool sensor ID

  # Weather station
  temperature:
    name: "${friendly_name} Temperature"
    device_class: temperature
    state_class: measurement
    unit_of_measurement: "°C"
  humidity:
    name: "${friendly_name} Humidity"
    device_class: humidity
    state_class: measurement
    unit_of_measurement: "%"
  wind_gust:
    name: "${friendly_name} Wind Gust"
    state_class: measurement
    unit_of_measurement: "m/s"
  wind_speed:
    name: "${friendly_name} Wind Speed"
    device_class: wind_speed
    state_class: measurement
    unit_of_measurement: "m/s"
  wind_direction:
    name: "${friendly_name} Wind Direction"
    device_class: wind_direction
    unit_of_measurement: "°"
  rain:
    name: "${friendly_name} Rain"
    device_class: precipitation
    state_class: total_increasing
    unit_of_measurement: "mm"
  uv:
    name: "${friendly_name} UV Index"
    state_class: measurement
  light:
    name: "${friendly_name} Light"
    device_class: illuminance
    state_class: measurement
    unit_of_measurement: "lx"
    filters:
      - multiply: 1000  # convert klx to lx
  rssi:
    name: "${friendly_name} RSSI"
    device_class: signal_strength
    state_class: measurement
    unit_of_measurement: "dBm"
    entity_category: diagnostic
  battery_ok:
    name: "${friendly_name} Battery"
    device_class: battery
    entity_category: diagnostic
  sensor_id:
    name: "${friendly_name} Sensor ID"
    entity_category: diagnostic

  # Pool / Spa Thermometer (PN 7000073)
  water_temperature:
    name: "${friendly_name} Pool Temperature"
    device_class: temperature
    state_class: measurement
    unit_of_measurement: "°C"
  pool_rssi:
    name: "${friendly_name} Pool RSSI"
    device_class: signal_strength
    state_class: measurement
    unit_of_measurement: "dBm"
    entity_category: diagnostic
  pool_battery_ok:
    name: "${friendly_name} Pool Battery"
    device_class: battery
    entity_category: diagnostic
  pool_sensor_id:
    name: "${friendly_name} Pool Sensor ID"
    entity_category: diagnostic
```

### `on_value` automation trigger

Fires on every successfully decoded frame. Use `x.pool_valid` to distinguish weather station from pool sensor frames.

```yaml
bresser_weather:
  # ...
  on_value:
    then:
      - lambda: |-
          if (x.pool_valid) {
            // Pool thermometer frame
            // x.pool_sensor_id       (std::string)
            // x.pool_rssi            (float, dBm)
            // x.pool_battery_ok      (bool)
            // x.water_temperature    (float, °C)  – check x.water_temperature_ok
            ESP_LOGI("weather", "Pool %s: %.1f°C", x.pool_sensor_id.c_str(), x.water_temperature);
          } else {
            // Weather station frame
            // x.sensor_id            (std::string, e.g. "00008BFC")
            // x.rssi                 (float, dBm)
            // x.battery_ok           (bool)
            // x.temperature          (float, °C)   – check x.temperature_ok
            // x.humidity             (float, %)    – check x.humidity_ok
            // x.wind_gust            (float, m/s)  – check x.wind_ok
            // x.wind_speed           (float, m/s)  – check x.wind_ok
            // x.wind_direction       (float, °)    – check x.wind_ok
            // x.rain                 (float, mm)   – check x.rain_ok
            // x.uv                   (float)       – check x.uv_ok
            // x.light                (float, klx)  – check x.light_ok
            ESP_LOGI("weather", "WS %s: %.1f°C", x.sensor_id.c_str(), x.temperature);
          }
```

## Available Sensors

### Weather Station

| Sensor           | Unit | Device Class    | Notes                         |
| ---------------- | ---- | --------------- | ----------------------------- |
| `temperature`    | °C   | temperature     |                               |
| `humidity`       | %    | humidity        |                               |
| `wind_speed`     | m/s  | wind_speed      |                               |
| `wind_gust`      | m/s  | —               |                               |
| `wind_direction` | °    | wind_direction  |                               |
| `rain`           | mm   | precipitation   | state_class: total_increasing |
| `uv`             | —    | —               |                               |
| `light`          | klx  | illuminance     | multiply 1000 for lx          |
| `rssi`           | dBm  | signal_strength | diagnostic                    |
| `battery_ok`     | —    | battery         | ON = battery low, diagnostic  |
| `sensor_id`      | —    | —               | diagnostic                    |

### Pool / Spa Thermometer (PN 7000073)

| Sensor              | Unit | Device Class    |            |
| ------------------- | ---- | --------------- | ---------- |
| `water_temperature` | °C   | temperature     |            |
| `pool_rssi`         | dBm  | signal_strength | diagnostic |
| `pool_battery_ok`   | —    | battery         | diagnostic |
| `pool_sensor_id`    | —    | —               | diagnostic |

## Finding Your Sensor ID

Enable DEBUG logging and watch the output after the device starts. The sensor ID appears on every received frame:

```
[bresser_weather] [Weather] ID=00008BFC T=21.3°C ...
[bresser_weather] [Pool]    ID=27761309 WaterTemp=24.1°C ...
```

Use the logged ID as `filter_sensor_id` / `filter_pool_sensor_id` to ignore nearby Bresser sensors you don't own.

## Data Update Frequency

Bresser sensors transmit approximately every 48 seconds. The component listens continuously and updates Home Assistant immediately on reception.

## Troubleshooting

### No data received

1. Check wiring — especially 3.3V power and all SPI connections.
2. Remove `filter_sensor_id` temporarily to receive from any sensor.
3. Enable DEBUG logging:
   ```yaml
   logger:
     level: DEBUG
   ```
4. Enable low-level radio and library logging:
   ```yaml
   esphome:
     platformio_options:
       build_flags:
         - -DCORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_VERBOSE
         - -DRADIOLIB_DEBUG_BASIC
         - -DRADIOLIB_DEBUG_PROTOCOL
   ```

### Disable frame-loss statistics

The component logs a frame-loss summary every 60 seconds at INFO level. To disable:

```yaml
esphome:
  platformio_options:
    build_flags:
      - -DBRESSER_DEBUG_STATS=0
```

## Credits

All sensor decoding and radio communication is handled by the [BresserWeatherSensorReceiver](https://github.com/matthias-bs/BresserWeatherSensorReceiver/tree/main) library by Matthias Prinke.

## License

MIT — see [LICENSE](LICENSE).
