# 第三方来源与许可

本项目包含多种许可证的第三方源码，各自适用的许可证、版权声明和来源文件保留在相应目录。此文件是导航，不替代原文，也不为整个仓库指定统一许可证。

| 内容 | 原有说明或许可证 |
| --- | --- |
| ESP32-S3板级组件 | `firmware/components/esp32_s3_touch_lcd_1_54/LICENSE` |
| Nofrendo / NES核心 | `firmware/components/nofrendo/COPYING`、`PORT.md` |
| 小智AI协议与绑定适配 | `firmware/main/core/xiaozhi.LICENSE`（MIT）；固定来源与范围见[小智AI](docs/firmware/xiaozhi_ai.md) |
| minimp3 | `firmware/components/mp3_decoder/LICENSE`；固定提交见[MP3说明](docs/firmware/mp3_music.md) |
| JPEG | `firmware/components/photo_jpeg/LICENSE.md`及对应来源说明 |
| GIF / giflib | `firmware/components/photo_gif/giflib/COPYING` |
| 录音DSP | `firmware/components/recorder_dsp/upstream/COPYING`及组件PORT说明 |
| 历法和配套第三方实现 | `firmware/main/third_party/lunar-LICENSE`、`chromium-LICENSE` |
| 厂商测试夹具 | `tooling/firmware/fixtures/vendor_touch_lcd154/LICENSE`及子目录声明 |

SDK和下载组件由[固定依赖清单](config/dependencies.json)及[依赖文档](docs/dependencies.md)管理，仓库保留必要补丁。字体生成所用字体需有相应使用/分发授权；普通构建复用已有字库，生成说明见对应工具。

自备音乐、照片、录音、密码数据和商业游戏ROM不随项目提供。项目原创部分尚未新增统一许可证；如需再分发或商业使用，应按各内容已有授权判断。
