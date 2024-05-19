#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zb_waterer.h"
#include "esp_zigbee_type.h"
#include "esp_check.h"
#include "driver.h"

static const char *TAG = "MAIN";
int meas_endpoints[] = {HA_ESP_SENSOR_1_ENDPOINT, HA_ESP_SENSOR_2_ENDPOINT, HA_ESP_SENSOR_3_ENDPOINT};

static int target_cluster_id = 0;

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

static void esp_app_water_consumption_handler(uint16_t value) {
    esp_zb_lock_acquire(portMAX_DELAY);
    ESP_LOGI(TAG, "Reporting water consumption - %d cycles", value);
    esp_zb_zcl_set_attribute_val(HA_CONSUMPTION_SENSOR_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &value, false);
    esp_zb_lock_release();
}

static void esp_app_humidity_sensor_handler(float humidity, int sensor_num)
{
    int endpoint = meas_endpoints[sensor_num];
    int16_t measured_value = (int16_t)(100 * humidity);
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &measured_value, false);
    esp_zb_lock_release();
}

static esp_err_t deferred_driver_init(void)
{
    init_driver(MEASUREMENT_INTERVAL_S, &esp_app_humidity_sensor_handler, &esp_app_water_consumption_handler);
    return ESP_OK;
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p     = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted");
            }
        } else {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d), type(0x%x)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size, message->attribute.data.type);
    if (message->info.dst_endpoint == HA_TARGET_HUMIDITY_ENDPOINT) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_OUTPUT) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_PRESENT_VALUE_ID) {
                float new_value = *(float *)message->attribute.data.value;
                ESP_LOGI(TAG, "Got new target humidity value - %0.2f", new_value);
                set_min_humidity(new_value);
            }
        }
    }
    if (message->info.dst_endpoint == HA_ONOFF_SWITCH_ENDPOINT) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                bool light_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : false;
                set_relay_state(light_state);
                esp_app_water_consumption_handler(10);
            }
        }
    }
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    default:
        ESP_LOGW(TAG, "Received Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}


/****** ROUTINES FOR CLUSTER CREATION */


static esp_zb_cluster_list_t *basic_identity_clusters_create() {
   esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_basic_cluster_cfg_t basic_config = {
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE
    };

    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&basic_config);
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, MANUFACTURER_NAME));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, MODEL_IDENTIFIER));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    esp_zb_identify_cluster_cfg_t identify_config = {
        .identify_time = 100
    };
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(&identify_config), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    return cluster_list;
}

static esp_zb_cluster_list_t *custom_humidity_sensor_clusters_create()
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
 
    esp_zb_humidity_meas_cluster_cfg_t measure_config = {
        .min_value = -1,
        .max_value = 100
    };

    ESP_ERROR_CHECK(esp_zb_cluster_list_add_humidity_meas_cluster(cluster_list, esp_zb_humidity_meas_cluster_create(&measure_config), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    return cluster_list;
}

static esp_zb_cluster_list_t *custom_consumption_clusters_create()
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_temperature_meas_cluster_cfg_t output_cfg = {
        .measured_value = 0,
        .min_value = 0,
        .max_value = 9999
    };

    ESP_ERROR_CHECK(esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, esp_zb_temperature_meas_cluster_create(&output_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    return cluster_list;
}

static esp_zb_cluster_list_t *custom_humidity_target_clusters_create()
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_analog_output_cluster_cfg_t output_cfg = {
        .out_of_service = false,
        .present_value = DEFAULT_MIN_HUMIDITY,
        .status_flags = 0
    };

    esp_zb_attribute_list_t * attrs = esp_zb_analog_output_cluster_create(&output_cfg);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_analog_output_cluster(cluster_list, attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    target_cluster_id = attrs->cluster_id;
    ESP_LOGD(TAG, "Created analog cluster output with id %i", attrs->cluster_id);
    return cluster_list;
}

static esp_zb_cluster_list_t *custom_on_off_clusters_create() {

    esp_zb_cluster_list_t *cluster_list = basic_identity_clusters_create();

    esp_zb_on_off_cluster_cfg_t onoff_cfg = {
        .on_off = false
    };

    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster(cluster_list, esp_zb_on_off_cluster_create(&onoff_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    return cluster_list;
        
}

/****** END CLUSTER CREATION */

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    esp_zb_ep_list_t *zb_endpoints = esp_zb_ep_list_create();
    #ifdef EXPOSE_RELAY_INPUT 
        esp_zb_endpoint_config_t endpoint_config = {
            .endpoint = HA_ONOFF_SWITCH_ENDPOINT,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
            .app_device_version = 0
        };

        esp_zb_ep_list_add_ep(zb_endpoints, custom_on_off_clusters_create(), endpoint_config);
    #endif

    for (int i = 0; i < SENSOR_COUNT; i++) {
        esp_zb_endpoint_config_t endpoint_config = {
            .endpoint = meas_endpoints[i],
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
            .app_device_version = 0
        };
        esp_zb_ep_list_add_ep(zb_endpoints, custom_humidity_sensor_clusters_create(), endpoint_config);
    }

    esp_zb_endpoint_config_t consumpton_endpoint_config = {
        .endpoint = HA_CONSUMPTION_SENSOR_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_ZGP_TEMPERATURE_SENSOR_DEV_ID,
        .app_device_version = 0
    };
    esp_zb_ep_list_add_ep(zb_endpoints, custom_consumption_clusters_create(), consumpton_endpoint_config);
    
    esp_zb_endpoint_config_t target_endpoint_config = {
        .endpoint = HA_TARGET_HUMIDITY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0
    };
    
    esp_zb_ep_list_add_ep(zb_endpoints, custom_humidity_target_clusters_create(), target_endpoint_config);

    esp_zb_device_register(zb_endpoints);

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

void app_main(void)
{   
    init_driver_immediate();
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
