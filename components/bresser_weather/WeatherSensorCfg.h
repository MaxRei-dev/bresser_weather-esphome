///////////////////////////////////////////////////////////////////////////////////////////////////
// WeatherSensorCfg.h  –  MODIFIED FOR POOL SENSOR SUPPORT
//
// Änderung gegenüber Original:
//   MAX_SENSORS_DEFAULT  1  →  2    (Wetterstation + Pool-Thermometer)
//   SENSOR_IDS_EXC            prüfen ob 0x792882A2 zufällig das Pool-Thermometer ist!
///////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(WEATHER_SENSOR_CFG_H)
#define WEATHER_SENSOR_CFG_H

#include <Arduino.h>

// ------------------------------------------------------------------------------------------------
// --- Weather Sensors ---
// ------------------------------------------------------------------------------------------------

// *** GEÄNDERT von 1 auf 2: ein Slot für Wetterstation, ein Slot für Pool-Thermometer ***
#define MAX_SENSORS_DEFAULT 2

// List of sensor IDs to be excluded - can be empty
#define SENSOR_IDS_EXC { }

// List of sensor IDs to be included - if empty, handle all available sensors
#define SENSOR_IDS_INC { }

// Maximum number of sensor IDs in include/exclude list
#define MAX_SENSOR_IDS 12

// Disable data type which will not be used to save RAM
#define WIND_DATA_FLOATINGPOINT
#define WIND_DATA_FIXEDPOINT

// Select appropriate sensor message format(s)
// BRESSER_6_IN_1 ist zwingend erforderlich für das Pool-Thermometer PN 7000073!
#define BRESSER_5_IN_1
#define BRESSER_6_IN_1
#define BRESSER_7_IN_1
#define BRESSER_LIGHTNING
#define BRESSER_LEAKAGE


// ------------------------------------------------------------------------------------------------
// --- Rain Gauge / Lightning sensor data retention during deep sleep ---
// ------------------------------------------------------------------------------------------------

#if !defined(INSIDE_UNITTEST)
    #if defined(ESP32)
        #define RAINGAUGE_USE_PREFS
        #define LIGHTNING_USE_PREFS
    #else
        #define RAINGAUGE_USE_PREFS
        #define LIGHTNING_USE_PREFS
    #endif
#endif

#if defined(USE_CC1101)
#define RADIO_CHIP CC1101
#elif defined(USE_SX1276)
#define RADIO_CHIP SX1276
#elif defined(USE_SX1262)
#define RADIO_CHIP SX1262
#elif defined(USE_LR1121)
#define RADIO_CHIP LR1121
#else
#pragma message("No radio chip selected!")
#endif


// ------------------------------------------------------------------------------------------------
// --- Debug Logging Output ---
// ------------------------------------------------------------------------------------------------
#if defined(ESP8266) || defined(ARDUINO_ARCH_RP2040)
    #define ARDUHAL_LOG_LEVEL_NONE      0
    #define ARDUHAL_LOG_LEVEL_ERROR     1
    #define ARDUHAL_LOG_LEVEL_WARN      2
    #define ARDUHAL_LOG_LEVEL_INFO      3
    #define ARDUHAL_LOG_LEVEL_DEBUG     4
    #define ARDUHAL_LOG_LEVEL_VERBOSE   5

    #if defined(ARDUINO_ARCH_RP2040) && defined(DEBUG_RP2040_PORT)
        #define DEBUG_PORT DEBUG_RP2040_PORT
    #elif defined(DEBUG_ESP_PORT)
        #define DEBUG_PORT DEBUG_ESP_PORT
    #endif

    #if !defined(CORE_DEBUG_LEVEL)
        #define CORE_DEBUG_LEVEL ARDUHAL_LOG_LEVEL_INFO
    #endif

    #if defined(DEBUG_PORT) && CORE_DEBUG_LEVEL > ARDUHAL_LOG_LEVEL_NONE
        #define log_e(...) { DEBUG_PORT.printf("%s(), l.%d: ",__func__, __LINE__); DEBUG_PORT.printf(__VA_ARGS__); DEBUG_PORT.println(); }
     #else
        #define log_e(...) {}
     #endif
    #if defined(DEBUG_PORT) && CORE_DEBUG_LEVEL > ARDUHAL_LOG_LEVEL_ERROR
        #define log_w(...) { DEBUG_PORT.printf("%s(), l.%d: ", __func__, __LINE__); DEBUG_PORT.printf(__VA_ARGS__); DEBUG_PORT.println(); }
     #else
        #define log_w(...) {}
     #endif
    #if defined(DEBUG_PORT) && CORE_DEBUG_LEVEL > ARDUHAL_LOG_LEVEL_WARN
        #define log_i(...) { DEBUG_PORT.printf("%s(), l.%d: ", __func__, __LINE__); DEBUG_PORT.printf(__VA_ARGS__); DEBUG_PORT.println(); }
     #else
        #define log_i(...) {}
     #endif
    #if defined(DEBUG_PORT) && CORE_DEBUG_LEVEL > ARDUHAL_LOG_LEVEL_INFO
        #define log_d(...) { DEBUG_PORT.printf("%s(), l.%d: ", __func__, __LINE__); DEBUG_PORT.printf(__VA_ARGS__); DEBUG_PORT.println(); }
     #else
        #define log_d(...) {}
     #endif
    #if defined(DEBUG_PORT) && CORE_DEBUG_LEVEL > ARDUHAL_LOG_LEVEL_DEBUG
        #define log_v(...) { DEBUG_PORT.printf("%s(), l.%d: ", __func__, __LINE__); DEBUG_PORT.printf(__VA_ARGS__); DEBUG_PORT.println(); }
     #else
        #define log_v(...) {}
     #endif
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define RECEIVER_CHIP "[" STR(RADIO_CHIP) "]"
#pragma message("Receiver chip: " RECEIVER_CHIP)
#pragma message("Pin config: RST->" STR(PIN_RECEIVER_RST) ", CS->" STR(PIN_RECEIVER_CS) ", GD0/G0/IRQ->" STR(PIN_RECEIVER_IRQ) ", GDO2/G1/GPIO->" STR(PIN_RECEIVER_GPIO) )

#endif
