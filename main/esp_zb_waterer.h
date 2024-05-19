
#include "esp_zigbee_core.h"

#define ED_AGING_TIMEOUT                ESP_ZB_ED_AGING_TIMEOUT_64MIN
#define ED_KEEP_ALIVE                   3000    /* 3000 millisecond */
#define MAX_CHILDREN                    10          /* the max amount of connected devices */
#define INSTALLCODE_POLICY_ENABLE       false       /* enable the install code policy for security */
#define HA_ONOFF_SWITCH_ENDPOINT        1           /* esp light switch device endpoint */
#define HA_ESP_SENSOR_1_ENDPOINT        10
#define HA_ESP_SENSOR_2_ENDPOINT        20
#define HA_ESP_SENSOR_3_ENDPOINT        30
#define HA_CONSUMPTION_SENSOR_ENDPOINT  40
#define HA_TARGET_HUMIDITY_ENDPOINT      50

#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK    /* Zigbee primary channel mask use in the example */


#define MEASUREMENT_INTERVAL_S 5

#define EXPOSE_RELAY_INPUT 

#define ESP_TEMP_SENSOR_UPDATE_INTERVAL (1)

#define MANUFACTURER_NAME               "\x0B""EP"
#define MODEL_IDENTIFIER                "\x09""GRAVE_PISSER"

#define ESP_ZB_ZED_CONFIG()                                         \
    {                                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                       \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
        .nwk_cfg.zed_cfg = {                                        \
            .ed_timeout = ED_AGING_TIMEOUT,                         \
            .keep_alive = ED_KEEP_ALIVE,                            \
        },                                                          \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }
