#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "dirent.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sd_protocol_defs.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "bsp/power_port.h"
#include "bsp/orientation.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "bsp_err_check.h"

#include "esp_lcd_st77916.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_codec_dev_defaults.h"
#include "iot_button.h"

static const char *TAG = "esp32_s3_touch_lcd_1_54";

/**
 * @brief I2C handle for BSP usage
 *
 * In IDF v5.4 you can call i2c_master_get_bus_handle(BSP_I2C_NUM, i2c_master_bus_handle_t *ret_handle)
 * from #include "esp_private/i2c_platform.h" to get this handle
 *
 * For IDF 5.2 and 5.3 you must call bsp_i2c_get_handle()
 */
static i2c_master_bus_handle_t i2c_handle = NULL;
static bool i2c_initialized = false;
static sdmmc_card_t *bsp_sdcard = NULL;    // Global uSD card handler
static esp_lcd_touch_handle_t tp;   // LCD touch handle
static esp_lcd_panel_handle_t panel_handle = NULL; // LCD panel handle
static esp_lcd_panel_io_handle_t io_handle = NULL;
static unsigned panel_orientation = 0; // GUI-only, never persisted.
#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_indev_t *disp_indev = NULL;
static lv_display_t *disp_drv = NULL;
#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static uint8_t brightness;
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static const audio_codec_data_if_t *i2s_data_if = NULL;  /* Codec data interface */
/* The BSP owns all objects it allocates; esp_codec_dev_delete does NOT delete
 * codec/control/GPIO/data interfaces. Callers serialize init/deinit and I/O. */
typedef struct {
    esp_codec_dev_handle_t device;
    const audio_codec_if_t *codec;
    const audio_codec_ctrl_if_t *control;
    const audio_codec_gpio_if_t *gpio;
} bsp_codec_owner_t;
static bsp_codec_owner_t speaker_owner = {0}, microphone_owner = {0};

#define LCD_OPCODE_READ_CMD         (0x0BULL)
#define LCD_LEDC_CH            CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH

/* Can be used for i2s_std_gpio_config_t and/or i2s_std_config_t initialization */
#define BSP_ES7210_CODEC_ADDR ES7210_CODEC_DEFAULT_ADDR
#define BSP_I2S_GPIO_CFG       \
    {                          \
        .mclk = BSP_I2S_MCLK,  \
        .bclk = BSP_I2S_SCLK,  \
        .ws = BSP_I2S_LCLK,    \
        .dout = BSP_I2S_DOUT,  \
        .din = BSP_I2S_DSIN,   \
        .invert_flags = {      \
            .mclk_inv = false, \
            .bclk_inv = false, \
            .ws_inv = false,   \
        },                     \
    }

/* This configuration is used by default in bsp_audio_init() */
#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate)                                                         \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = BSP_I2S_GPIO_CFG,                                                                 \
    }

esp_err_t bsp_i2c_init(void)
{
    /* I2C was initialized before */
    if (i2c_initialized)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
    };
    BSP_ERROR_CHECK_RETURN_ERR(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle));

    i2c_initialized = true;

    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    BSP_ERROR_CHECK_RETURN_ERR(i2c_del_master_bus(i2c_handle));
    i2c_initialized = false;
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    bsp_i2c_init();
    return i2c_handle;
}

sdmmc_card_t *bsp_sdcard_get_handle(void)
{
    return bsp_sdcard;
}

void bsp_sdcard_get_sdmmc_host(const int slot, sdmmc_host_t *config)
{
    assert(config);

    sdmmc_host_t host_config = SDMMC_HOST_DEFAULT();

    memcpy(config, &host_config, sizeof(sdmmc_host_t));
}

void bsp_sdcard_get_sdspi_host(const int slot, sdmmc_host_t *config)
{
    assert(config);
    memset(config, 0, sizeof(sdmmc_host_t));
    ESP_LOGE(TAG, "SD card SPI mode is not supported by HW!");
}

void bsp_sdcard_sdmmc_get_slot(const int slot, sdmmc_slot_config_t *config)
{
    assert(config);
    memset(config, 0, sizeof(sdmmc_slot_config_t));

    config->clk = BSP_SD_CLK;
    config->cmd = BSP_SD_CMD;
    config->d0 = BSP_SD_D0;
    config->d1 = GPIO_NUM_NC;
    config->d2 = GPIO_NUM_NC;
    config->d3 = GPIO_NUM_NC;
    config->d4 = GPIO_NUM_NC;
    config->d5 = GPIO_NUM_NC;
    config->d6 = GPIO_NUM_NC;
    config->d7 = GPIO_NUM_NC;
    config->cd = SDMMC_SLOT_NO_CD;
    config->wp = SDMMC_SLOT_NO_WP;
    config->width = 1;
    config->flags = 0;
}

void bsp_sdcard_sdspi_get_slot(const spi_host_device_t spi_host, sdspi_device_config_t *config)
{
    assert(config);
    memset(config, 0, sizeof(sdspi_device_config_t));
    ESP_LOGE(TAG, "SD card SPI mode is not supported by HW!");
}

esp_err_t bsp_sdcard_sdmmc_mount(bsp_sdcard_cfg_t *cfg)
{
    sdmmc_host_t sdhost = {0};
    sdmmc_slot_config_t sdslot = {0};
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        // Reinsertion/recovery must never turn an I/O fault into data loss.
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    assert(cfg);

    if (!cfg->mount) {
        cfg->mount = &mount_config;
    }

    if (!cfg->host) {
        bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &sdhost);
        cfg->host = &sdhost;
    }

    if (!cfg->slot.sdmmc) {
        bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &sdslot);
        cfg->slot.sdmmc = &sdslot;
    }

#if !CONFIG_FATFS_LONG_FILENAMES
    ESP_LOGW(TAG, "Warning: Long filenames on SD card are disabled in menuconfig!");
#endif

    return esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, cfg->host, cfg->slot.sdmmc, cfg->mount, &bsp_sdcard);
}

esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg)
{
    ESP_LOGE(TAG, "SD card SPI mode is not supported by HW!");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sdcard_mount(void)
{
    bsp_sdcard_cfg_t cfg = {0};
    const esp_err_t err = bsp_sdcard_sdmmc_mount(&cfg);
    if (err != ESP_OK && !bsp_sdcard) (void)bsp_sdcard_quiesce();
    return err;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (!bsp_sdcard) return ESP_OK;
    esp_err_t ret = ESP_OK;

    ret |= esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, bsp_sdcard);
    bsp_sdcard = NULL;

    return ret;
}

esp_err_t bsp_sdcard_ready(void)
{
    if (!bsp_sdcard) return ESP_ERR_INVALID_STATE;
    sdmmc_command_t cmd = {
        .opcode = MMC_SEND_STATUS,
        .arg = MMC_ARG_RCA(bsp_sdcard->rca),
        .flags = SCF_CMD_AC | SCF_RSP_R1,
        .timeout_ms = 100,
    };
    const esp_err_t err = bsp_sdcard->host.do_transaction(bsp_sdcard->host.slot, &cmd);
    if (err != ESP_OK) return err;
    if (cmd.error != ESP_OK) return cmd.error;
    // Native SD R1: reject address/erase/write-protect/CRC/illegal-command,
    // ECC/controller/general errors and authentication sequence errors.
    const uint32_t errors = 0xFDFFE008u;
    if (cmd.response[0] & errors) return ESP_FAIL;
    return (cmd.response[0] & MMC_R1_READY_FOR_DATA) &&
           MMC_R1_CURRENT_STATE_STATUS(cmd.response[0]) == MMC_R1_CURRENT_STATE_TRAN
               ? ESP_OK : ESP_ERR_NOT_FINISHED;
}

esp_err_t bsp_sdcard_quiesce(void)
{
    if (bsp_sdcard) return ESP_ERR_INVALID_STATE;
    // Vendor schematic: VDD and six 10K pull-ups connect directly to 3V3;
    // CD is unconnected. There is no software-controlled SD power switch.
    // Disconnect the MCU's drive and internal pulls, NOT the shared power rail.
    const gpio_config_t pins = {
        .pin_bit_mask = (1ULL << BSP_SD_CLK) | (1ULL << BSP_SD_CMD) |
                        (1ULL << BSP_SD_D0) | (1ULL << BSP_SD_D1) |
                        (1ULL << BSP_SD_D2) | (1ULL << BSP_SD_D3),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&pins);
}

static void bsp_codec_release(bsp_codec_owner_t *owner)
{
    if (owner->device) esp_codec_dev_delete(owner->device);
    if (owner->codec) audio_codec_delete_codec_if(owner->codec);
    if (owner->control) audio_codec_delete_ctrl_if(owner->control);
    if (owner->gpio) audio_codec_delete_gpio_if(owner->gpio);
    *owner = (bsp_codec_owner_t){0};
}

static esp_err_t bsp_i2s_release(i2s_chan_handle_t *channel)
{
    if (!*channel) return ESP_OK;
    i2s_chan_info_t info = {0};
    esp_err_t err = i2s_channel_get_info(*channel, &info);
    if (err != ESP_OK) return err;
    // close() may already have stopped one side of the duplex pair. Do not
    // disable it twice; delete also works for a partially initialized channel.
    if (info.is_enabled) {
        err = i2s_channel_disable(*channel);
        if (err != ESP_OK) return err;
    }
    err = i2s_del_channel(*channel);
    if (err == ESP_OK) *channel = NULL;
    return err; // retain a failed handle so cleanup can be retried safely
}

esp_err_t bsp_audio_deinit(void)
{
    // Only after the media worker has stopped I/O; the shared I2C bus belongs
    // to touch/RTC/etc. and must stay alive. Delete devices before interfaces.
    bsp_codec_release(&speaker_owner);
    bsp_codec_release(&microphone_owner);
    if (i2s_data_if) {
        audio_codec_delete_data_if(i2s_data_if);
        i2s_data_if = NULL;
    }
    esp_err_t tx_err = bsp_i2s_release(&i2s_tx_chan);
    esp_err_t rx_err = bsp_i2s_release(&i2s_rx_chan);
    gpio_set_direction(BSP_POWER_AMP_IO, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_POWER_AMP_IO, 0);
    return tx_err != ESP_OK ? tx_err : rx_err;
}

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    if (i2s_data_if && i2s_tx_chan && i2s_rx_chan) return ESP_OK;
    // Recover incomplete previous initialization; no stale deleted handles.
    esp_err_t ret = bsp_audio_deinit();
    if (ret != ESP_OK) return ret;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ret = i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan);
    if (ret != ESP_OK) goto err;

    const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(22050);
    const i2s_std_config_t *p_i2s_cfg = i2s_config ? i2s_config : &std_cfg_default;
    ESP_GOTO_ON_ERROR(i2s_channel_init_std_mode(i2s_tx_chan, p_i2s_cfg), err, TAG, "I2S TX initialization failed");
    ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_tx_chan), err, TAG, "I2S TX enabling failed");
    ESP_GOTO_ON_ERROR(i2s_channel_init_std_mode(i2s_rx_chan, p_i2s_cfg), err, TAG, "I2S RX initialization failed");
    ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_rx_chan), err, TAG, "I2S RX enabling failed");
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM, .rx_handle = i2s_rx_chan, .tx_handle = i2s_tx_chan,
    };
    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!i2s_data_if) { ret = ESP_ERR_NO_MEM; goto err; }
    return ESP_OK;
err:
    (void)bsp_audio_deinit();
    return ret;
}

const audio_codec_data_if_t *bsp_audio_get_codec_itf(void)
{
    return i2s_data_if;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    if (speaker_owner.device) return speaker_owner.device;
    const audio_codec_data_if_t *i2s_data_if = bsp_audio_get_codec_itf();
    if (i2s_data_if == NULL) {
        /* Initilize I2C */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_i2c_init());
        /* Configure I2S peripheral and Power Amplifier */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_audio_init(NULL));
        i2s_data_if = bsp_audio_get_codec_itf();
    }
    assert(i2s_data_if);

    const audio_codec_gpio_if_t *gpio_if = speaker_owner.gpio = audio_codec_new_gpio();
    if (!gpio_if) goto err;

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_handle,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = speaker_owner.control = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!i2c_ctrl_if) goto err;

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = speaker_owner.codec = es8311_codec_new(&es8311_cfg);
    if (!es8311_dev) goto err;

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = i2s_data_if,
    };
    speaker_owner.device = esp_codec_dev_new(&codec_dev_cfg);
    if (speaker_owner.device) return speaker_owner.device;
err:
    bsp_codec_release(&speaker_owner);
    if (!microphone_owner.device) (void)bsp_audio_deinit();
    return NULL;
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    if (microphone_owner.device) return microphone_owner.device;
    const audio_codec_data_if_t *i2s_data_if = bsp_audio_get_codec_itf();
    if (i2s_data_if == NULL) {
        /* Initilize I2C */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_i2c_init());
        /* Configure I2S peripheral and Power Amplifier */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_audio_init(NULL));
        i2s_data_if = bsp_audio_get_codec_itf();
    }
    assert(i2s_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = BSP_ES7210_CODEC_ADDR,
        .bus_handle = i2c_handle,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = microphone_owner.control = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!i2c_ctrl_if) goto err;

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = i2c_ctrl_if,
    };
    const audio_codec_if_t *es7210_dev = microphone_owner.codec = es7210_codec_new(&es7210_cfg);
    if (!es7210_dev) goto err;

    esp_codec_dev_cfg_t codec_es7210_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es7210_dev,
        .data_if = i2s_data_if,
    };
    microphone_owner.device = esp_codec_dev_new(&codec_es7210_dev_cfg);
    if (microphone_owner.device) return microphone_owner.device;
err:
    bsp_codec_release(&microphone_owner);
    if (!speaker_owner.device) (void)bsp_audio_deinit();
    return NULL;
}

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    ESP_LOGI(TAG, "SPI BUS init");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_SCLK,
        .mosi_io_num = BSP_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));


    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = BSP_LCD_CS;
    io_config.dc_gpio_num = BSP_LCD_DC;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 2;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;

    // Attach the LCD to the SPI bus
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_SPI_HOST, &io_config, ret_io);
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_new_panel_st7789(*ret_io, &panel_config, ret_panel);

    esp_lcd_panel_reset(*ret_panel);
    esp_lcd_panel_init(*ret_panel);
    esp_lcd_panel_invert_color(*ret_panel, true);
    esp_lcd_panel_disp_on_off(*ret_panel, true);

    return ESP_OK;
}

static esp_err_t orientation_commands(unsigned turns)
{
    const bsp_orientation_panel_t mode = bsp_orientation_panel(turns);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_handle, mode.swap_xy), TAG, "orientation swap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, mode.mirror_x, mode.mirror_y), TAG, "orientation mirror failed");
    return esp_lcd_panel_set_gap(panel_handle, mode.x_gap, mode.y_gap);
}

esp_err_t bsp_display_set_orientation(unsigned turns)
{
    if(turns > 3) return ESP_ERR_INVALID_ARG;
    if(!panel_handle) return ESP_ERR_INVALID_STATE;
    if(turns == panel_orientation) return ESP_OK;
    // SPI parameter writes drain outstanding DMA. The LVGL lock prevents a
    // flush/touch read between these commands and publishing the new mapping.
    const esp_err_t error = orientation_commands(turns);
    if(error != ESP_OK) {
        const esp_err_t rollback = orientation_commands(panel_orientation);
        if(rollback != ESP_OK) ESP_LOGE(TAG, "orientation rollback failed: %s", esp_err_to_name(rollback));
        return error;
    }
    panel_orientation = turns;
    return ESP_OK;
}

unsigned bsp_display_get_orientation(void) { return panel_orientation; }

static void orientation_touch(esp_lcd_touch_handle_t handle, uint16_t *x, uint16_t *y,
                              uint16_t *strength, uint8_t *count, uint8_t capacity)
{
    (void)handle; (void)strength;
    uint8_t valid = 0;
    for(uint8_t i = 0; i < *count && i < capacity; ++i) {
        if(x[i] >= 240 || y[i] >= 240) continue;
        x[valid] = x[i]; y[valid] = y[i];
        if(strength) strength[valid] = strength[i];
        bsp_orientation_point(panel_orientation, &x[valid], &y[valid]);
        ++valid;
    }
    *count = valid;
}

esp_err_t bsp_touch_new(const bsp_display_cfg_t *cfg, esp_lcd_touch_handle_t *ret_touch)
{
    assert(cfg != NULL);
    /* Initilize I2C */
    BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());


    i2c_master_bus_handle_t i2c_handle = NULL;
    i2c_master_get_bus_handle(BSP_I2C_NUM,  &i2c_handle);

    /* Initialize touch HW */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = 240,
        .y_max = 240,
        .rst_gpio_num = BSP_LCD_TOUCH_RST,
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .process_coordinates = orientation_touch,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = cfg->touch_flags.swap_xy,
            .mirror_x = cfg->touch_flags.mirror_x,
            .mirror_y = cfg->touch_flags.mirror_y,
        },
    };
    
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {};
    tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
    tp_io_config.control_phase_bytes = 1;
    tp_io_config.dc_bit_offset = 0;
    tp_io_config.lcd_cmd_bits = 8;
    tp_io_config.flags.disable_control_phase = 1;
    tp_io_config.scl_speed_hz = 400 * 1000;
    // Touch runs in LVGL: neither a busy audio bus nor a lost completion may
    // leave the entire UI waiting forever. Retrying is handled by the adapter.
    tp_io_config.transaction_timeout_ms = 20;
    esp_lcd_new_panel_io_i2c((i2c_master_bus_handle_t)i2c_handle, &tp_io_config, &tp_io_handle);
    return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, ret_touch);
}


#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_display_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);

    bsp_display_config_t disp_config = {0};
    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new(&disp_config, &panel_handle, &io_handle));

    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = panel_handle,
        .panel_io = io_handle,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = cfg->rotation,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            // FIX: draw buffer 放 internal RAM(DMA-capable),避免 st7789 SPI DMA
            // 从 PSRAM 传输时需分配 internal 中转 buffer 而 internal RAM 不足,
            // 导致 spicommon_dma_setup_priv_buffer 失败 -> LVGL flush 死循环卡死。
            .buffer_height = 20,
            .use_psram = false,
            .enable_ppa_accel = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = cfg->tear_avoid_mode,
    };

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp)
    {
        return NULL;
    }

    return disp;
}

static esp_lcd_touch_interrupt_callback_t power_touch_original_irq;
static void power_touch_irq(esp_lcd_touch_handle_t handle) {
    // One notification per asserted level. Only the GUI re-arms it after
    // draining the packet; a stuck-low line must not starve the scheduler.
    gpio_intr_disable(BSP_LCD_TOUCH_INT);
    if(!bsp_power_touch_enabled())return;
    if(power_touch_original_irq)power_touch_original_irq(handle);
    bsp_power_touch_irq();
}
void bsp_power_touch_gate(bool active) {
    gpio_intr_disable(BSP_LCD_TOUCH_INT);
    bsp_power_touch_reset();
    // The forced screen-on read drains stale packets before re-arming IRQ.
    if(!active)return;
    bsp_power_touch_wakeup();
}
bool bsp_power_touch_pending(void) {
    return bsp_power_touch_enabled() && gpio_get_level(BSP_LCD_TOUCH_INT)==0;
}
void bsp_power_touch_rearm(void) {
    if(bsp_power_touch_enabled() && gpio_get_level(BSP_LCD_TOUCH_INT)!=0)
        gpio_intr_enable(BSP_LCD_TOUCH_INT);
}
static esp_err_t power_touch_after_sleep(int64_t elapsed,void *arg) {
    (void)arg;
    // GPIO48 is a digital IO, outside the RTC IO wake-status bitmap. A button
    // wake may also request this harmless touch read; perform it in the GUI task.
    if(elapsed>0 && (esp_sleep_get_wakeup_causes() & (1UL<<ESP_SLEEP_WAKEUP_GPIO)))
        bsp_power_touch_wakeup();
    return ESP_OK;
}
static lv_indev_t *bsp_display_indev_init(const bsp_display_cfg_t *cfg, lv_display_t *disp)
{
    assert(cfg != NULL);
    BSP_ERROR_CHECK_RETURN_NULL(bsp_touch_new(cfg, &tp));
    assert(tp);

    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);

    lv_indev_t *input=esp_lv_adapter_register_touch(&touch_cfg);
    if(input) {
        power_touch_original_irq=tp->config.interrupt_callback;
        esp_lcd_touch_register_interrupt_callback(tp,power_touch_irq);
        ESP_ERROR_CHECK(gpio_wakeup_enable(tp->config.int_gpio_num,GPIO_INTR_LOW_LEVEL));
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
        esp_pm_sleep_cbs_register_config_t callbacks={.exit_cb=power_touch_after_sleep};
        ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&callbacks));
    }
    return input;
}
#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

esp_err_t bsp_display_brightness_init(void)
{
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = BSP_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE
    };
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_USE_RC_FAST_CLK
    };

    BSP_ERROR_CHECK_RETURN_ERR(ledc_timer_config(&LCD_backlight_timer));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_channel_config(&LCD_backlight_channel));

    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    } else if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    int flipped_brightness = brightness_percent;

    brightness = (uint8_t)flipped_brightness;

    ESP_LOGD(TAG, "Setting flipped LCD backlight: %d%% (original: %d%%)", flipped_brightness, brightness_percent);

    uint32_t duty_cycle = (1023 * flipped_brightness) / 100;
    BSP_ERROR_CHECK_RETURN_ERR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    if (disp_drv == NULL)
    {
        ESP_LOGE(TAG, "disp_drv is not initialized");
        return -1;
    }

    return brightness; // stored unit is already percent (0..100)
}

esp_err_t bsp_display_backlight_off(void)
{
    ESP_LOGI(TAG, "Backlight off");
    return bsp_display_brightness_set(0);
}

// BEGIN BLE-preserving display standby (worker-only, paired suspend/resume)
static bool display_paused = false;
static bool panel_needs_wake = false;
static bool touch_needs_wake = false;

esp_err_t bsp_display_suspend(void)
{
    if (display_paused) return ESP_OK;
    // The acknowledgement includes completion of LVGL/touch callbacks.
    esp_err_t ret = bsp_power_pause(1000);
    if (ret != ESP_OK) return ret;
    display_paused = true;
    // Track attempted commands too: an I/O error can leave hardware half asleep.
    panel_needs_wake = true;
    ret = esp_lcd_panel_disp_on_off(panel_handle, false);
    if (ret != ESP_OK) return ret;
    // SPI parameter commands drain previously queued color DMA transfers.
    ret = esp_lcd_panel_io_tx_param(io_handle, LCD_CMD_SLPIN, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    if (ret != ESP_OK) return ret;
    if (tp) {
        touch_needs_wake = true;
        const uint8_t sleep_mode = 0x03; // board SensorLib CST816 sleep register
        ret = esp_lcd_panel_io_tx_param(tp->io, 0xE5, &sleep_mode, 1);
        if (ret != ESP_OK) return ret;
    }
    // Power already set PWM duty to zero. Keep LEDC routing intact for wake.
    return ESP_OK;
}

esp_err_t bsp_display_resume(void)
{
    if (!display_paused) return ESP_OK;
    esp_err_t ret;
    if (panel_needs_wake) {
        ret = esp_lcd_panel_io_tx_param(io_handle, LCD_CMD_SLPOUT, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        if (ret != ESP_OK) return ret;
        ret = esp_lcd_panel_disp_on_off(panel_handle, true);
        if (ret != ESP_OK) return ret;
        panel_needs_wake = false;
    }
    if (touch_needs_wake && tp) {
        if (tp->config.rst_gpio_num == GPIO_NUM_NC) return ESP_ERR_INVALID_STATE;
        // Match the installed CST816 driver's proven reset timing, no reallocation.
        ret = gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset);
        if (ret != ESP_OK) return ret;
        vTaskDelay(pdMS_TO_TICKS(200));
        ret = gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset);
        if (ret != ESP_OK) return ret;
        vTaskDelay(pdMS_TO_TICKS(200));
        // Drain any stale touch packet before LVGL re-enables input.
        ret = esp_lcd_touch_read_data(tp);
        if (ret != ESP_OK) return ret;
        touch_needs_wake = false;
    }
    ret = esp_lv_adapter_resume();
    if (ret == ESP_OK) display_paused = false;
    return ret;
}
// END BLE-preserving display standby

esp_err_t bsp_display_backlight_on(void)
{
    ESP_LOGI(TAG, "Backlight on");
    return bsp_display_brightness_set(100);
}

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

lv_display_t *bsp_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0}};
    cfg.lv_adapter_cfg.task_stack_size = 10 * 1024;
    // This firmware requires PSRAM XiP and permits external task stacks (also
    // checked by lvgl_psram_threads.c). Keep the GUI stack out of the radio/DMA
    // heap; the adapter still allocates its task control block internally.
    cfg.lv_adapter_cfg.stack_in_psram = true;
    // Touch responsiveness: the LVGL task polls the touch controller once per
    // loop, then sleeps up to task_max_delay_ms when the UI is idle. With the
    // default 15 ms cap, a fresh finger-down can wait most of that window
    // before it is read, which feels like "the screen ignored my tap". Halving
    // the idle sleep roughly halves that worst-case latency and makes taps
    // register far more reliably, at negligible idle-power cost. Bump the task
    // priority one step so a busy render never starves touch sampling.
    cfg.lv_adapter_cfg.task_max_delay_ms = 1000; // normal 8ms cap and IRQ wake in power port
    cfg.lv_adapter_cfg.task_priority     = 7;
    return bsp_display_start_with_config(&cfg);
}

lv_display_t *bsp_display_start_with_config(bsp_display_cfg_t *cfg)
{
    lv_display_t *disp;

    assert(cfg != NULL);
    BSP_ERROR_CHECK_RETURN_NULL(esp_lv_adapter_init(&cfg->lv_adapter_cfg));

    BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

    BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(cfg, disp), NULL);

    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_brightness_init());

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    disp_drv = disp;
    return disp;
}


lv_display_t *bsp_display_get_disp_dev(void)
{
    return disp_drv;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

esp_err_t bsp_display_lock(int32_t timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    esp_lv_adapter_unlock();
}

#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

esp_err_t bsp_spiffs_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_BSP_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
        .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
#ifdef CONFIG_BSP_SPIFFS_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    BSP_ERROR_CHECK_RETURN_ERR(ret_val);

    size_t total = 0, used = 0;
    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_unmount(void)
{
    return esp_vfs_spiffs_unregister(CONFIG_BSP_SPIFFS_PARTITION_LABEL);
}

/**
 * @brief A universal function to retrieve a list of files with specific extensions in a designated directory
 * 
 * @param dir_path path (example "/sdcard")
 * @param extension target extension (example ".jpg", ".avi", ".png")
 * @param out 
 * @return esp_err_t 
 */
esp_err_t get_file_list_by_ext(const char *dir_path, const char *extension, generic_file_list_t *out)
{
    if (!dir_path || !extension || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    out->list  = NULL;
    out->count = 0;

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return ESP_FAIL;
    }

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { 
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcasecmp(ext, extension) == 0) {
                count++;
            }
        }
    }

    if (count == 0) {
        closedir(dir);
        return ESP_ERR_NOT_FOUND;
    }

    char **list = (char **)malloc(sizeof(char *) * count);
    if (!list) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    rewinddir(dir);
    int idx = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcasecmp(ext, extension) == 0) {

                size_t len = strlen(dir_path) + strlen(entry->d_name) + 2;
                char *full_path = (char *)malloc(len);
                if (!full_path) {
 
                    for (int i = 0; i < idx; i++) free(list[i]);
                    free(list);
                    closedir(dir);
                    return ESP_ERR_NO_MEM;
                }
                snprintf(full_path, len, "%s/%s", dir_path, entry->d_name);
                list[idx++] = full_path;
            }
        }
    }

    closedir(dir);
    out->list  = list;
    out->count = idx;
    return ESP_OK;
}
