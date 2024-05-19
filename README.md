This is a working code for Zigbee plant waterer working on ESP32-H2 board bound to Home Assistant.
May be useful for esp-zigbee-sdk noobs like me who are having hard time going through the examples trying to implement real-life devices.
There are a few shortcuts to avoid ZHA quirks by using standard clusters (water consumption reporting as temperature, lol)

The unusual thing about this build is having multiple humidity sensors and only one watering pump. Set up is to keep the minimum humidity under control with a chance to drown the rest. Easily overcome-able by specifying just 1 sensor.

I hope the code should be self-descriptive

Project is using https://github.com/espressif/vscode-esp-idf-extension/tree/master/docs, please refer to their docs on environment setup (can be tricky !)