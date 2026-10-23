# Patch the two adapter scheduling points without changing managed sources or
# copying the whole upstream implementation. Relative includes still resolve
# against the original source; its target retains every upstream dependency.
idf_component_get_property(_adapter_lib espressif__esp_lvgl_adapter COMPONENT_LIB)
idf_component_get_property(_adapter_dir espressif__esp_lvgl_adapter COMPONENT_DIR)
set(_power_port "${CMAKE_CURRENT_LIST_DIR}")
get_target_property(_adapter_sources ${_adapter_lib} SOURCES)
list(FILTER _adapter_sources EXCLUDE REGEX "(^|/)esp_lv_adapter\\.c$|(^|/)esp_lv_adapter_input_touch\\.c$")
set_property(TARGET ${_adapter_lib} PROPERTY SOURCES "${_adapter_sources}")
target_sources(${_adapter_lib} PRIVATE "${_power_port}/lvgl_power_adapter.c" "${_power_port}/lvgl_power_touch.c")
target_include_directories(${_adapter_lib} PRIVATE "${_power_port}/include")
target_compile_definitions(${_adapter_lib} PRIVATE
    YUMO_LV_ADAPTER_SOURCE="${_adapter_dir}/src/adapter/esp_lv_adapter.c"
    YUMO_LV_TOUCH_SOURCE="${_adapter_dir}/src/input/esp_lv_adapter_input_touch.c")

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_vfs_usb_serial_jtag_get_vfs")

# Move only LVGL-created task stacks to PSRAM, retaining the upstream OS port.
# Its mutexes/TCBs and the LCD DMA buffers continue to use internal memory.
idf_component_get_property(_lvgl_lib lvgl__lvgl COMPONENT_LIB)
idf_component_get_property(_lvgl_dir lvgl__lvgl COMPONENT_DIR)
get_target_property(_lvgl_sources ${_lvgl_lib} SOURCES)
set(_lvgl_os_sources ${_lvgl_sources})
list(FILTER _lvgl_os_sources INCLUDE REGEX "(^|/)lv_freertos\\.c$")
list(LENGTH _lvgl_os_sources _lvgl_os_count)
if(NOT _lvgl_os_count EQUAL 1)
    message(FATAL_ERROR "Expected one pinned LVGL FreeRTOS source for the PSRAM stack adapter")
endif()
list(FILTER _lvgl_sources EXCLUDE REGEX "(^|/)lv_freertos\\.c$")
set_property(TARGET ${_lvgl_lib} PROPERTY SOURCES "${_lvgl_sources}")
target_sources(${_lvgl_lib} PRIVATE "${_power_port}/lvgl_psram_threads.c")
target_compile_definitions(${_lvgl_lib} PRIVATE
    YUMO_LV_FREERTOS_SOURCE="${_lvgl_dir}/src/osal/lv_freertos.c")
