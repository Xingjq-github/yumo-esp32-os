# New firmware: three-layer architecture (core / ui / apps).
# Legacy launcher/theme placeholders are excluded from the active firmware.
set(MAIN_SRCS
    "main.cpp"
    # core layer
    "core/event_bus.cpp"
    "core/system_service.cpp"
    "core/settings_store.cpp"
    "core/clock_discipline.cpp"
    "core/gpio_isr_service.cpp"
    "core/connectivity.cpp"
    "core/lan_hotspot.cpp"
    "core/lan_web.cpp"
    "core/speed_test.cpp"
    "core/speed_test_store.cpp"
    "core/garden_model.cpp"
    "core/garden_store.cpp"
    "core/garden_service.cpp"
    "core/ble_hid.cpp"
    "core/ble_keyboard_client.cpp"
    "core/ble_peer_store.cpp"
    "core/keyboard_input.cpp"
    "core/power_mgr.cpp"
    "core/power_runtime.cpp"
    "core/deep_sleep_wake.c"
    "core/display_cast.cpp"
    "core/display_cast_codec.c"
    "core/display_cast_console.cpp"
    "core/screenshot.cpp"
    "core/screenshot_png.c"
    "core/button_mgr.cpp"
    "core/audio_mgr.cpp"
    "core/xiaozhi_service.cpp"
    "core/xiaozhi_cloud.cpp"
    "core/xiaozhi_protocol.cpp"
    "core/xiaozhi_transport.cpp"
    "core/xiaozhi_audio.cpp"
    "core/audio_recorder.cpp"
    "core/worker_wake.cpp"
    "core/game_audio.cpp"
    "core/chiptune_score.cpp"
    "core/chiptune_player.cpp"
    "core/mp3_player.cpp"
    "core/usb_serial_phy.cpp"
    "core/usb_storage.cpp"
    "core/sd_media.cpp"
    "core/fc_emulator.cpp"
    "core/fc_saves.cpp"
    "core/fc_save_store.c"
    "core/battery_mgr.cpp"
    "core/qmi8658.cpp"
    "core/screen_orientation.cpp"
    "core/charset_conv.cpp"
    "core/storage_browser.cpp"
    "core/file_operations.cpp"
    "core/file_manager.cpp"
    "core/knowledge_store.cpp"
    "core/pattern_format.cpp"
    "core/pattern_store.cpp"
    "core/pattern_data.cpp"
    "core/photo_loader.cpp"
    "core/photo_frame_cache.cpp"
    "core/gif_player.cpp"
    "core/photo_png.c"
    "core/vault_crypto.cpp"
    "core/vault_sha256.c"
    "core/vault_gcm.c"
    "core/vault_storage.cpp"
    "core/vault_json.cpp"
    "core/vault_sync.cpp"
    "core/vault_connection.cpp"
    "core/password_vault.cpp"
    "core/gbk_table.c"
    # ui layer
    "ui/window_mgr.cpp"
    "ui/screen_protection.cpp"
    "ui/gesture.cpp"
    "ui/home_screen.cpp"
    "ui/sysinfo_overlay.cpp"
    "ui/theme.cpp"
    "ui/compact_ui.cpp"
    "ui/vault_qr.cpp"
    "ui/vault_pattern.cpp"
    "ui/garden_scene.cpp"
    "ui/text_input.cpp"
    "ui/quick_panel.cpp"
    "ui/external_keyboard.cpp"
    "ui/bluetooth_pairing.cpp"
    "ui/boot_animation.cpp"
    "ui/almanac_clock.cpp"
    "ui/lv_font_almanac_24.c"
    "ui/lv_font_menu_14.c"
    "ui/lv_font_code_13.c"
    "ui/lv_font_app_icons.c"
    "ui/lv_font_clock_84.c"
    "ui/lv_font_cjk_16.c"
    "ui/lv_font_reading_15.c"
    "ui/lv_font_music_22.c"
    # apps layer
    "apps/xiaozhi.cpp"
    "apps/flashlight.cpp"
    "apps/watchface.cpp"
    "apps/timer.cpp"
    "apps/garden.cpp"
    "apps/settings.cpp"
    "apps/lan.cpp"
    "apps/speed_test.cpp"
    "apps/settings_wifi.cpp"
    "apps/settings_bluetooth.cpp"
    "apps/settings_system.cpp"
    "apps/settings_storage.cpp"
    "apps/calendar.cpp"
    "apps/dino.cpp"
    "apps/gravity_maze.cpp"
    "apps/morse.cpp"
    "apps/bt_keyboard_mouse.cpp"
    "apps/bt_music.cpp"
    "apps/chiptune.cpp"
    "apps/mp3_music.cpp"
    "apps/text_reader.cpp"
    "apps/programming.cpp"
    "apps/design_patterns.cpp"
    "apps/randomizer.cpp"
    "apps/photo_viewer.cpp"
    "apps/file_manager.cpp"
    "apps/password_vault.cpp"
    "apps/recorder.cpp"
    "apps/fc_player.cpp"
    "apps/fc_player_saves.cpp"
)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/vault_sync_key.cmake")
yumo_configure_vault_sync_key("${CMAKE_CURRENT_LIST_DIR}/../vault-sync-key.properties"
                              "${CMAKE_CURRENT_BINARY_DIR}/generated")

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "." "core" "ui" "apps" "${CMAKE_CURRENT_BINARY_DIR}/generated"
    EMBED_FILES "core/lan_web.html"
    REQUIRES
        lvgl
        nofrendo
        recorder_dsp
        mp3_decoder
        espressif__esp_tinyusb
        esp32_s3_touch_lcd_1_54
        esp_codec_dev
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_system
        app_update
        esp_timer
        esp_pm
        esp-tls
        esp_http_client
        esp_http_server
        mbedtls
        espressif__cjson
        driver
        esp_driver_usb_serial_jtag
        esp_stdio
        esp_lcd
        esp_hw_support
        lwip
        button
        bt
        esp_hid
        yumo_ble_store
        esp_lvgl_adapter
        spi_flash
        esp_new_jpeg
        photo_jpeg
        photo_gif
        espressif__libpng
    PRIV_REQUIRES
        bootloader_support
        esp_psram
        fatfs
        esp_hal_wdt
        espressif__esp_audio_effects
        espressif__esp_audio_codec
        espressif__esp_websocket_client
        espressif__mqtt
)

include("${CMAKE_CURRENT_LIST_DIR}/../components/esp32_s3_touch_lcd_1_54/power_adapter.cmake")

# Board-wide GPIO ISR ownership; do not patch SDK or managed drivers.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=gpio_install_isr_service"
    "-Wl,-u,__wrap_gpio_install_isr_service")

# Intercept the actual SDK boundaries without modifying third-party sources.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=rtc_clk_cal" "-Wl,-u,__wrap_rtc_clk_cal"
    "-Wl,--wrap=sntp_set_system_time" "-Wl,-u,__wrap_sntp_set_system_time")

# usb_storage.cpp provides strong TinyUSB MSC callbacks and both mount hooks.
# The optional esp_tinyusb filesystem glue is consequently not linked. Keep
# upstream transport intact and filesystem ownership in the existing SD service.
