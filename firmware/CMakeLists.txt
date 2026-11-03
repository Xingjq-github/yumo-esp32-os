cmake_minimum_required(VERSION 3.5)

# Legacy sample trees are retained on disk but are not part of this OS.
set(EXCLUDE_COMPONENTS app_framework brookesia_core brookesia_app_clock
    brookesia_app_settings brookesia_app_squareline_demo)

set(PROJECT_VER "1")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies.cmake")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(01_factory)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/nimble_hid_input.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/nvs_blob_recovery.cmake")
