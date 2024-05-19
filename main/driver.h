#include "driver/gpio.h"
#include "soc/adc_channel.h"

#define RELAY_PIN GPIO_NUM_4
#define RELAY_ON_TIME_S 5
#define RELAY_MIN_TIME_BETWEEN_CYCLES_M 5

#define SENSOR_POWER_PIN GPIO_NUM_0

#define SENSOR_POWER_UP_TIME_MS 100

#define SENSOR_COUNT 2

#define DRY_VOLTAGE 2100
#define WET_VOLTAGE 850

#define DEFAULT_MIN_HUMIDITY 40.0f

typedef void (*esp_humidity_sensor_callback_t)(float temperature, int device);
typedef void (*esp_water_consumption_callback_t)(uint32_t seconds);

esp_err_t init_driver_immediate();
esp_err_t init_driver(int interval_s, esp_humidity_sensor_callback_t cb, esp_water_consumption_callback_t water_cb);
esp_err_t set_relay_state(bool on);
void set_min_humidity(float value);