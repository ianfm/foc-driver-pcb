cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3")
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(foc-pcb-firmware)