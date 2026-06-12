import os
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import sensor, binary_sensor, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_TEMPERATURE,
    CONF_HUMIDITY,
    CONF_TRIGGER_ID,
    CONF_ON_VALUE,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)
from esphome.core import CORE

AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]

# ── Weather station config keys ───────────────────────────────────────────────
CONF_WIND_GUST         = "wind_gust"
CONF_WIND_SPEED        = "wind_speed"
CONF_WIND_DIRECTION    = "wind_direction"
CONF_RAIN              = "rain"
CONF_UV                = "uv"
CONF_LIGHT             = "light"
CONF_RSSI              = "rssi"
CONF_BATTERY_OK        = "battery_ok"
CONF_SENSOR_ID         = "sensor_id"
CONF_FILTER_SENSOR_ID  = "filter_sensor_id"

# ── Pool thermometer config keys ──────────────────────────────────────────────
CONF_WATER_TEMPERATURE      = "water_temperature"
CONF_POOL_RSSI              = "pool_rssi"
CONF_POOL_BATTERY_OK        = "pool_battery_ok"
CONF_POOL_SENSOR_ID         = "pool_sensor_id"
CONF_FILTER_POOL_SENSOR_ID  = "filter_pool_sensor_id"

# ── Radio / pin config keys ───────────────────────────────────────────────────
CONF_RADIO   = "radio"
CONF_PINS    = "pins"
CONF_CS_PIN  = "cs"
CONF_IRQ_PIN = "irq"
CONF_GPIO_PIN = "gpio"
CONF_RST_PIN  = "rst"
CONF_SCK_PIN  = "sck"
CONF_MISO_PIN = "miso"
CONF_MOSI_PIN = "mosi"

# ── Custom units (not yet in esphome.const for all ESPHome versions) ──────────
UNIT_METER_PER_SECOND = "m/s"
UNIT_MILLIMETER       = "mm"
UNIT_DEGREES          = "°"
UNIT_KILOLUX          = "klx"
UNIT_DBM              = "dBm"

# ── C++ class references ──────────────────────────────────────────────────────
bresser_weather_ns      = cg.esphome_ns.namespace("bresser_weather")
BresserWeatherComponent = bresser_weather_ns.class_("BresserWeatherComponent", cg.Component)
WeatherData             = bresser_weather_ns.struct("WeatherData")
WeatherDataTrigger      = bresser_weather_ns.class_(
    "WeatherDataTrigger", automation.Trigger.template(WeatherData)
)

RADIO_TYPES = {
    "cc1101": "CC1101",
    "sx1262": "SX1262",
    "sx1276": "SX1276",
    "lr1121": "LR1121",
}

# Alle unterstützten Radios werden von RadioLib über SPI angesprochen.
_SPI_PIN_KEYS = (CONF_SCK_PIN, CONF_MISO_PIN, CONF_MOSI_PIN)


def _validate_spi_pins(config):
    """RadioLib übernimmt die SPI-Pins NICHT aus dem ESPHome 'spi:'-Block.
    Fehlen sie, greift RadioLib auf (oft falsche) Default-Pins zurück und der
    Radio-Init hängt erst zur Laufzeit. Daher hier schon zur Compile-Zeit prüfen.
    """
    pin_config = config[CONF_PINS]
    missing = [k for k in _SPI_PIN_KEYS if k not in pin_config]
    if missing and config[CONF_RADIO] in RADIO_TYPES:
        raise cv.Invalid(
            f"Das Radio '{config[CONF_RADIO]}' wird von RadioLib über SPI angesprochen, "
            f"aber RadioLib übernimmt die Pins NICHT aus dem ESPHome 'spi:'-Block. "
            f"Bitte 'sck', 'miso' und 'mosi' unter 'pins:' angeben "
            f"(fehlend: {', '.join(missing)})."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BresserWeatherComponent),

        # ── Radio hardware ────────────────────────────────────────────────────
        cv.Required(CONF_RADIO): cv.one_of(*RADIO_TYPES, lower=True),
        cv.Required(CONF_PINS): cv.Schema(
            {
                cv.Required(CONF_CS_PIN):  pins.internal_gpio_output_pin_number,
                cv.Required(CONF_IRQ_PIN): pins.internal_gpio_input_pin_number,
                cv.Required(CONF_GPIO_PIN): pins.internal_gpio_input_pin_number,
                cv.Required(CONF_RST_PIN): cv.Any(
                    cv.int_range(min=-1, max=-1),
                    pins.internal_gpio_output_pin_number,
                ),
                # SPI-Bus-Pins für RadioLib (optional). Wenn gesetzt, wird das
                # globale Arduino-SPI vor ws_.begin() explizit darauf initialisiert,
                # sonst greift RadioLib auf die (oft falschen) Default-Pins zurück.
                cv.Optional(CONF_SCK_PIN):  pins.internal_gpio_output_pin_number,
                cv.Optional(CONF_MISO_PIN): pins.internal_gpio_input_pin_number,
                cv.Optional(CONF_MOSI_PIN): pins.internal_gpio_output_pin_number,
            }
        ),

        # ── Weather station sensors ───────────────────────────────────────────
        cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_HUMIDITY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_WIND_GUST): sensor.sensor_schema(
            unit_of_measurement=UNIT_METER_PER_SECOND,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_WIND_SPEED): sensor.sensor_schema(
            unit_of_measurement=UNIT_METER_PER_SECOND,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_WIND_DIRECTION): sensor.sensor_schema(
            unit_of_measurement=UNIT_DEGREES,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_RAIN): sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLIMETER,
            accuracy_decimals=1,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_UV): sensor.sensor_schema(
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_LIGHT): sensor.sensor_schema(
            unit_of_measurement=UNIT_KILOLUX,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_RSSI): sensor.sensor_schema(
            unit_of_measurement=UNIT_DBM,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_BATTERY_OK): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY,
        ),
        cv.Optional(CONF_SENSOR_ID): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_FILTER_SENSOR_ID): cv.hex_uint32_t,

        # ── Pool / Spa Thermometer sensors (PN 7000073) ───────────────────────
        cv.Optional(CONF_WATER_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_POOL_RSSI): sensor.sensor_schema(
            unit_of_measurement=UNIT_DBM,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_POOL_BATTERY_OK): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY,
        ),
        cv.Optional(CONF_POOL_SENSOR_ID): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_FILTER_POOL_SENSOR_ID): cv.hex_uint32_t,

        # ── Automation trigger ────────────────────────────────────────────────
        cv.Optional(CONF_ON_VALUE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(WeatherDataTrigger),
            }
        ),
    }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_spi_pins,
)


async def to_code(config):

    script_path = os.path.join(os.path.dirname(__file__), "pre_build.py")
    cg.add_platformio_option("extra_scripts", [f"pre:{script_path}"])

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ── Weather station sensors ───────────────────────────────────────────────
    if CONF_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_TEMPERATURE])
        cg.add(var.set_temperature_sensor(sens))

    if CONF_HUMIDITY in config:
        sens = await sensor.new_sensor(config[CONF_HUMIDITY])
        cg.add(var.set_humidity_sensor(sens))

    if CONF_WIND_GUST in config:
        sens = await sensor.new_sensor(config[CONF_WIND_GUST])
        cg.add(var.set_wind_gust_sensor(sens))

    if CONF_WIND_SPEED in config:
        sens = await sensor.new_sensor(config[CONF_WIND_SPEED])
        cg.add(var.set_wind_speed_sensor(sens))

    if CONF_WIND_DIRECTION in config:
        sens = await sensor.new_sensor(config[CONF_WIND_DIRECTION])
        cg.add(var.set_wind_direction_sensor(sens))

    if CONF_RAIN in config:
        sens = await sensor.new_sensor(config[CONF_RAIN])
        cg.add(var.set_rain_sensor(sens))

    if CONF_UV in config:
        sens = await sensor.new_sensor(config[CONF_UV])
        cg.add(var.set_uv_sensor(sens))

    if CONF_LIGHT in config:
        sens = await sensor.new_sensor(config[CONF_LIGHT])
        cg.add(var.set_light_sensor(sens))

    if CONF_RSSI in config:
        sens = await sensor.new_sensor(config[CONF_RSSI])
        cg.add(var.set_rssi_sensor(sens))

    if CONF_BATTERY_OK in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_BATTERY_OK])
        cg.add(var.set_battery_sensor(sens))

    if CONF_SENSOR_ID in config:
        sens = await text_sensor.new_text_sensor(config[CONF_SENSOR_ID])
        cg.add(var.set_sensor_id_text_sensor(sens))

    if CONF_FILTER_SENSOR_ID in config:
        cg.add(var.set_filter_sensor_id(config[CONF_FILTER_SENSOR_ID]))

    # ── Pool / Spa Thermometer sensors ────────────────────────────────────────
    if CONF_WATER_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_WATER_TEMPERATURE])
        cg.add(var.set_water_temperature_sensor(sens))

    if CONF_POOL_RSSI in config:
        sens = await sensor.new_sensor(config[CONF_POOL_RSSI])
        cg.add(var.set_pool_rssi_sensor(sens))

    if CONF_POOL_BATTERY_OK in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_POOL_BATTERY_OK])
        cg.add(var.set_pool_battery_sensor(sens))

    if CONF_POOL_SENSOR_ID in config:
        sens = await text_sensor.new_text_sensor(config[CONF_POOL_SENSOR_ID])
        cg.add(var.set_pool_sensor_id_text_sensor(sens))

    if CONF_FILTER_POOL_SENSOR_ID in config:
        cg.add(var.set_filter_pool_sensor_id(config[CONF_FILTER_POOL_SENSOR_ID]))

    # ── Automation trigger ────────────────────────────────────────────────────
    for conf in config.get(CONF_ON_VALUE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(WeatherData, "x")], conf)

    # ── Library dependencies ──────────────────────────────────────────────────
    cg.add_platformio_option("lib_deps", ["matthias-bs/BresserWeatherSensorReceiver@0.41.0"])
    cg.add_platformio_option("lib_deps", ["jgromes/RadioLib@7.6.0"])
    cg.add_platformio_option("lib_deps", ["vshymanskyy/Preferences@2.2.2"])
    cg.add_platformio_option("lib_deps", ["bblanchon/ArduinoJson@7.4.3"])

    # ── Build flags: radio chip + receiver pins ───────────────────────────────
    radio_define = RADIO_TYPES[config[CONF_RADIO]]
    cg.add_build_flag(f"-DUSE_{radio_define}")

    pin_config = config[CONF_PINS]
    cg.add_build_flag(f"-DPIN_RECEIVER_CS={pin_config[CONF_CS_PIN]}")
    cg.add_build_flag(f"-DPIN_RECEIVER_IRQ={pin_config[CONF_IRQ_PIN]}")
    cg.add_build_flag(f"-DPIN_RECEIVER_GPIO={pin_config[CONF_GPIO_PIN]}")
    cg.add_build_flag(f"-DPIN_RECEIVER_RST={pin_config[CONF_RST_PIN]}")

    if CONF_SCK_PIN in pin_config:
        cg.add_build_flag(f"-DPIN_SPI_SCK={pin_config[CONF_SCK_PIN]}")
    if CONF_MISO_PIN in pin_config:
        cg.add_build_flag(f"-DPIN_SPI_MISO={pin_config[CONF_MISO_PIN]}")
    if CONF_MOSI_PIN in pin_config:
        cg.add_build_flag(f"-DPIN_SPI_MOSI={pin_config[CONF_MOSI_PIN]}")

    cg.add_platformio_option("lib_ldf_mode", "deep+")
