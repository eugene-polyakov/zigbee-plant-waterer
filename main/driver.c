#include "driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "DRIVER";
int sensor_pins[] = {ADC1_GPIO1_CHANNEL, ADC1_GPIO2_CHANNEL, ADC1_GPIO3_CHANNEL};
bool calibration_enabled = true;
adc_cali_handle_t calibration_handles[] = {0, 0, 0}; // seems like an overkill, one calibration func should be plenty
static int target_min_humidity = DEFAULT_MIN_HUMIDITY;
static float minutes_since_last_pump = 0.0f;
static uint16_t water_consumption_cycles = 0;

#define ATTN ADC_ATTEN_DB_12

static esp_humidity_sensor_callback_t report_ptr;
static esp_water_consumption_callback_t consumption_ptr;
static int interval;
adc_oneshot_unit_handle_t adc1_handle;
static bool driver_initialized = false;

esp_err_t set_relay_state(bool on) {
        esp_err_t gpio_result = gpio_set_level(RELAY_PIN, on ? 0 : 1);    // inverted relay state since my custom-built relay driver is inverted                     
        if (gpio_result != ESP_OK) {            
            ESP_LOGI(TAG, "Error %d when setting gpio pin", gpio_result);
        }
    return gpio_result;
}

static float calculate_humidity(int value) {
    if (value > DRY_VOLTAGE) { return -1.0f; };
    if (value < WET_VOLTAGE) { return -1.0f; };
    return ((float)(value - DRY_VOLTAGE) / (WET_VOLTAGE - DRY_VOLTAGE)) * 100.0;
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    ESP_LOGD(TAG, "calibration scheme version is %s", "Curve Fitting");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}


static void measure_task(void *pvParameters) {
    
    while (true) {

        ESP_LOGD(TAG, "Starting measurement, powering up");
        gpio_set_level(SENSOR_POWER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POWER_UP_TIME_MS));
        int adc_raw;

        float min_measured_humidity = 100.0f;

        for (int i = 0; i < SENSOR_COUNT; i++) {
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, sensor_pins[i], &adc_raw));
            ESP_LOGD(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, sensor_pins[i], adc_raw);

            int voltage;
            int value = adc_raw;

            if (calibration_enabled) {
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(calibration_handles[i], adc_raw, &voltage));
                ESP_LOGD(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, sensor_pins[i], voltage);               
                value = voltage;
            }

            if (report_ptr) {
                float rep = calculate_humidity(value);
                ESP_LOGD(TAG, "Reporting value %.2f for channel %d", rep, i);
                report_ptr(rep, i);
            }
            if (value > 0 && value < min_measured_humidity) { min_measured_humidity = value; }
        }
        ESP_LOGD(TAG, "Completed measurement");
        gpio_set_level(SENSOR_POWER_PIN, 0);

        if (min_measured_humidity < target_min_humidity && minutes_since_last_pump > RELAY_MIN_TIME_BETWEEN_CYCLES_M) {
            ESP_LOGI(TAG, "Turning pump on");
            set_relay_state(true);
            vTaskDelay(pdMS_TO_TICKS(RELAY_ON_TIME_S * 1000));
            ESP_LOGI(TAG, "Turning pump off");
            set_relay_state(false);
            water_consumption_cycles += 1; // RELAY_ON_TIME_S;
            consumption_ptr(water_consumption_cycles);
            minutes_since_last_pump = 0;
        } 
        
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));

        minutes_since_last_pump += interval / 60.0f;
    }
    
}

esp_err_t init_driver_immediate() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((1ULL<<SENSOR_POWER_PIN) | (1ULL<<RELAY_PIN));
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    gpio_set_direction(RELAY_PIN, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(SENSOR_POWER_PIN, GPIO_MODE_INPUT_OUTPUT);

    gpio_set_level(SENSOR_POWER_PIN, 0);
    set_relay_state(false);
    return ESP_OK;
}

esp_err_t init_driver(int interval_s, esp_humidity_sensor_callback_t cb, esp_water_consumption_callback_t water_cb) {

    report_ptr = cb;
    consumption_ptr = water_cb;
    interval = interval_s;
    ESP_LOGI(TAG, "Driver init");

    if (driver_initialized) {
        return ESP_OK;
    }

    driver_initialized = true;

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ATTN,
    };

    for (int i = 0 ; i < SENSOR_COUNT; i++) {
        calibration_enabled &= adc_calibration_init(ADC_UNIT_1, sensor_pins[0], ATTN, &calibration_handles[i]);
    }

    ESP_LOGI(TAG, "Calibration %s", calibration_enabled ? "Enabled" : "Disabled");

    for (int i = 0; i < SENSOR_COUNT; i++) {    
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, sensor_pins[i], &config));
    }

    return (xTaskCreate(measure_task, "Measure_main", 8192, NULL, 10, NULL) == true) ? ESP_OK : ESP_FAIL;
}

void set_min_humidity(float value) {
    target_min_humidity = value;
}