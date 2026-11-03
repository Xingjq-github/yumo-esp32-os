idf_component_register(SRCS "yumo_ble_store.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES bt nvs_flash)

# Use the pinned NimBLE store arrays and callbacks; only replace their on-flash
# representation. Never patch the SDK or wrap unrelated NVS calls.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/store/config/src")
foreach(symbol ble_store_config_conf_init
               ble_store_config_persist_our_secs
               ble_store_config_persist_peer_secs
               ble_store_config_persist_cccds
               ble_store_config_persist_rpa_recs
               ble_store_config_persist_csfcs)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${symbol}")
endforeach()
