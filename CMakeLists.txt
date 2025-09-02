# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/lib/mqtt
    ${CMAKE_SOURCE_DIR}/lib/pca9557
    ${CMAKE_SOURCE_DIR}/lib/QRCode
    ${CMAKE_SOURCE_DIR}/lib/u8g2
    ${CMAKE_SOURCE_DIR}/lib/u8g2-hal-esp-idf
    ${CMAKE_SOURCE_DIR}/lib/i2cdev
    ${CMAKE_SOURCE_DIR}/lib/esp_idf_lib_helpers
    ${CMAKE_SOURCE_DIR}/lib/esp-modbus-master
   
)
project(IOT_FARM)
