# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
if(EXISTS "${CMAKE_SOURCE_DIR}/components")
	set(EXTRA_COMPONENT_DIRS components)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The release number the gauge shows ("v35"): version.txt holds the number of the next image-frames-vN release
# (the Racing My Car platform's scripts/publish-firmware.mjs refuses a tag that does not match it).
file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" RMC_FW_NUM LIMIT_COUNT 1)
set(PROJECT_VER "v${RMC_FW_NUM}")
add_compile_options("-Wno-format")
project(rmc_gauge)
