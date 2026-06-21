# Gateway firmware CMake configuration
# Top-level project for the ESP32-S3 host side of the gateway.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake OPTIONAL)

# If ESP-Zigbee-SDK is available in the IDF_COMPONENT_MANAGER ecosystem,
# it will be auto-fetched via idf_component.yml. Otherwise, point
# EXTRA_COMPONENT_DIRS at a local checkout.
set(EXTRA_COMPONENT_DIRS "../components")

project(esp_agrinet_gateway)
