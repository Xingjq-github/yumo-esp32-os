# 雨墨文档导航

## 开始使用

- [项目首页](../README.md) · [当前功能基线](current-state.md)
- [环境、初始化、编译与烧录](build.md)
- [配置与密钥](configuration.md) · [依赖和许可](dependencies.md)
- [硬件与引脚](firmware/hardware.md) · [SD卡目录](../resources/sdcard/README.md)

## 阅读代码

- [学习路线](learning.md) · [目录结构](structure.md)
- [固件架构与生命周期](firmware/firmware_architecture.md)
- [25个应用总览](firmware/overview.md) · [贡献说明](../CONTRIBUTING.md)
- [验证范围与检查入口](validation.md)

## 设备功能

| 分类 | 文档 |
| --- | --- |
| 密码与连接 | [密码本](firmware/password_vault.md)、[同步协议](server/PROTOCOL.md)、[蓝牙连接与手柄](firmware/bluetooth_connections.md) |
| 时钟与工具 | [主题/时钟/万年历](firmware/themes_calendar_games.md)、[秒表](firmware/timer.md)、[RTC校时学习](firmware/network_time.md)、[测速](firmware/speed_test.md) |
| 音乐与录音 | [MP3与歌词](firmware/mp3_music.md)、[8Bit音乐](firmware/chiptune.md)、[录音界面与DSP](firmware/recorder_ui_dsp_20260919.md) |
| 游戏 | [FC操作](firmware/fc_touch_controls.md)、[FC显示](firmware/fc_rendering.md)、[存档](firmware/fc_saves.md)、[兼容范围](firmware/photo_fc_compatibility.md)、[萌芽物语](firmware/garden.md) |
| 文件与图片 | [文本/相册](firmware/reader_photo_random_20260921.md)、[图片解码与缓存](firmware/photo_rendering.md)、[学习资料与文件工具](firmware/learning_files_morse.md) |
| 系统与网络 | [局域网共享/硬盘/密码网页](firmware/lan_hotspot_20260920.md)、[屏幕旋转](firmware/screen_orientation.md)、[快捷设置](firmware/quick_settings.md)、[电源管理](firmware/power_management.md) |
| USB与桌面 | [USB读卡器](firmware/usb_storage.md)、[USB投屏](desktop/pc-display.md)、[UI演示](desktop/ui-preview.md) |

## 服务与部署

- [PC服务](server/pc.md) · [NAS容器部署](server/nas.md)
- [配置说明](configuration.md) · [协议说明](server/PROTOCOL.md)
- [第三方来源与许可](../THIRD_PARTY_NOTICES.md)

## 2026-11-02设备版本

- [小智AI：在线语音与240×240界面](firmware/xiaozhi_ai.md)
- [蓝牙鼠标、16条绑定与手柄](firmware/bluetooth_connections.md)
- [九点图形密码](firmware/password_vault.md) · [公网HTTPS同步](firmware/vault_public_https_20261031.md)
- [Wi-Fi连接优化](firmware/wifi_connection_latency.md) · [LCD静置保护](firmware/screen_protection.md)
- [K8s/K9s知识库](firmware/learning_files_morse.md) · [便携升级脚本](firmware/portable_flash.md)
