cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.tuned.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_file_share)
