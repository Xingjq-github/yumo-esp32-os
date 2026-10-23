"""Guard the radio memory trade-off without shrinking display or BLE resources."""
from pathlib import Path
import unittest

ROOT=(Path(__file__).resolve().parents[2]/'firmware')

def config(name):
    result={}
    for line in (ROOT/name).read_text(encoding='utf-8').splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            key,value=line.split('=',1);result[key]=value
        elif line.startswith('# CONFIG_') and line.endswith(' is not set'):
            result[line[2:-11]]='n'
    return result

class WifiMemoryContracts(unittest.TestCase):
    def test_internal_heap_budget_and_external_host_remain_explicit(self):
        for name in ('sdkconfig','sdkconfig.defaults'):
            options=config(name)
            for key,value in {
                'ESP_WIFI_IRAM_OPT':'n','ESP_WIFI_RX_IRAM_OPT':'n',
                'ESP_WIFI_STATIC_RX_BUFFER_NUM':'6','ESP_WIFI_STATIC_TX_BUFFER_NUM':'6',
                'SPIRAM_MALLOC_RESERVE_INTERNAL':'65536','SPIRAM_MALLOC_ALWAYSINTERNAL':'0',
                'SPIRAM_TRY_ALLOCATE_WIFI_LWIP':'y','BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL':'y',
                'BT_NIMBLE_HOST_TASK_STACK_SIZE':'6144',
                'SPIRAM_FETCH_INSTRUCTIONS':'y','SPIRAM_RODATA':'y',
            }.items():
                self.assertEqual(options.get('CONFIG_'+key),value,(name,key))
        options=config('sdkconfig')
        self.assertEqual(options['CONFIG_ESP_COEX_SW_COEXIST_ENABLE'],'y')
        self.assertEqual(options['CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY'],'n')

    def test_failure_is_recoverable_not_asserting_or_disabling_bluetooth(self):
        source=(ROOT/'main/core/connectivity.cpp').read_text(encoding='utf-8')
        self.assertNotIn('esp_netif_create_default_wifi_sta(',source)
        self.assertIn('station=esp_netif_new(&config)',source)
        self.assertIn('return startFailed("initialization",err)',source)
        self.assertIn('return startFailed("start",err)',source)
        self.assertNotIn('BleHid::',source)
        self.assertNotIn('nimble_port_deinit',source)

    def test_gui_stacks_use_psram_without_shrinking_render_or_dma_capacity(self):
        bsp=ROOT/'components/esp32_s3_touch_lcd_1_54'
        display=(bsp/'esp32_s3_touch_lcd_1_54.c').read_text(encoding='utf-8')
        self.assertIn('cfg.lv_adapter_cfg.stack_in_psram = true',display)
        self.assertIn('cfg.lv_adapter_cfg.task_stack_size = 10 * 1024',display)
        self.assertIn('.buffer_height = 20',display)
        self.assertIn('.require_double_buffer = true',display)
        options=config('sdkconfig')
        self.assertEqual(options['CONFIG_LV_DRAW_THREAD_STACK_SIZE'],'40960')
        self.assertEqual(options['CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT'],'2')
        source=(bsp/'lvgl_psram_threads.c').read_text(encoding='utf-8')
        self.assertIn('xTaskCreateWithCaps(',source)
        self.assertIn('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT',source)
        self.assertIn('#define vTaskDelete vTaskDeleteWithCaps',source)
        self.assertIn('#include YUMO_LV_FREERTOS_SOURCE',source)
        self.assertNotIn('MALLOC_CAP_INTERNAL',source)
        for flag in ('SPIRAM_XIP_FROM_PSRAM','SPIRAM_FETCH_INSTRUCTIONS',
                     'SPIRAM_RODATA','FREERTOS_TASK_CREATE_ALLOW_EXT_MEM'):
            self.assertIn('!CONFIG_'+flag,source)
            self.assertEqual(options['CONFIG_'+flag],'y')
        cmake=(bsp/'power_adapter.cmake').read_text(encoding='utf-8')
        self.assertIn('lvgl_psram_threads.c',cmake)
        self.assertIn('YUMO_LV_FREERTOS_SOURCE=',cmake)
        self.assertIn('if(NOT _lvgl_os_count EQUAL 1)',cmake)

if __name__=='__main__':unittest.main(verbosity=2)
