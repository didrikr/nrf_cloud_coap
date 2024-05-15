cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(coap_mqtt_comparison)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE src/main.c)

target_sources_ifdef(CONFIG_NRF_CLOUD_MQTT app PRIVATE src/cloud_mqtt.c)
target_sources_ifdef(CONFIG_NRF_CLOUD_COAP app PRIVATE src/cloud_coap.c)
