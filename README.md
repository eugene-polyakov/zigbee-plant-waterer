This is a working code for Zigbee plant waterer working on ESP32-H2 board bound to Home Assistant.
There are a few shortcuts to avoid ZHA quirks by using standard clusters (water consumption reporting as temperature, lol)
I hope the code should be self-descriptive

Project is using https://github.com/espressif/vscode-esp-idf-extension/tree/master/docs, please refer to their docs on environment setup (can be tricky !)