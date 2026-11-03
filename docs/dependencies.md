# 依赖版本、适配与许可

仓库不附带工具安装包、SDK 或下载的组件包。项目实际依赖保存在 `config/dependencies.json`，请按此清单准备，不用下载旧工程中未参与编译的其他组件。

## ESP-IDF

- 官方仓库：`https://github.com/espressif/esp-idf.git`
- 标签：`v6.1`
- 实际提交：`fff9895c82d744c7237be8847347bdd1b07c6643`
- NimBLE 子模块提交：`139cada0ae932957fa06ba37d17e3c9c2c95c773`

必须连同子模块获取。核对主仓库提交后激活 SDK，再执行：

```bat
python tooling\apply_sdk_patch.py
```

这会在使用者自行安装的 SDK 中应用 `patches/esp-idf/nimble.patch`。该补丁保留本工程的蓝牙生命周期处理，只有三行；脚本先验证原文件/补丁后文件摘要，已应用时直接通过，不重复修改，未知版本时拒绝修改。建议为本项目使用独立的 SDK 工作副本。

该补丁不是完整依赖包，不包含工具或预编译库。正常构建会检查补丁是否存在，不会自动修改外部 SDK。

## 组件

```bat
python tooling\prepare_dependencies.py
```

脚本从 Espressif 官方组件存储准备26个锁定组件（24个注册表包、2个固定Git源码清单），校验 `CHECKSUMS.json` 摘要和文件内容。下载路径 `dependencies/managed_components/` 被 Git 忽略，可以在构建后删除。

唯一使用中的组件适配是 `waveshare/pcf85063a` 的 CMake 依赖声明，保存在 `patches/components/waveshare__pcf85063a/CMakeLists.txt`；下载后只对指定基线应用此文件。清单同时记录修改前后摘要。

SDK 自带的无线、PHY 等预编译依赖仍由 SDK 安装/子模块获取；本仓库不重新分发这些包。第三方安装来源与许可由其官方仓库或组件发布页说明。

## 已移植源码

`firmware/components/` 下的板级适配、Nofrendo、GIF/JPEG 解码与录音 DSP 是现有功能所需的源码，随项目保留；对应目录中的 LICENSE、COPYING、NOTICE、UPSTREAM、PORT 或 provenance 文件保留来源与许可。它们与下载的工具环境不同，不能作为“缓存”清理。

教学资料和 SD 卡演示资源见 `resources/` 的说明。用户的商业游戏 ROM、密码数据、录音和照片不包含在此源码库。整体对外分发时应遵守各组件原许可证，不能把不同许可证的第三方代码全部声明为同一新许可证。

MP3组件使用`lieff/minimp3`，固定提交`ea99364f61c14656440e8d77e9c233ccf3124633`；源码及CC0许可随`firmware/components/mp3_decoder/`保留，组件仅启用Layer III。用途、预算和音乐字库说明见[MP3音乐](firmware/mp3_music.md)。

## Java、桌面和可选开发依赖

- 服务端 Java 依赖由 `server/pom.xml` 固定，Maven 首次构建从官方中央仓库获取。
- PC 投屏 Python 依赖见 `desktop/pc-display/requirements.txt`；Tkinter 由 Python 安装提供。
- 网页自动化测试依赖见 `tooling/server/requirements-browser.txt`，Playwright 浏览器另行安装；普通使用不需要浏览器测试环境。
- 重新制作字库需要自行提供有相应使用/分发授权的字体及 `fonttools`、`freetype-py`；普通固件编译使用已有 C 字库，不需要额外安装字体。

## 小智与蓝牙新增依赖（2026-11-03）

- ESP Audio Codec 2.5.0：注册表CHECKSUMS摘要锁定，含音频编解码库，许可证随组件下载。
- WebSocket 1.8.0：`espressif/esp-protocols` 提交 `70bf122fc8bc74622dcf7e15233b5b2af9088df5`。
- MQTT 1.1.0：`espressif/esp-mqtt` 提交 `1a1e5788a5cf57a0f44a3c6c061407f6c9be1026`。

后两项使用 `config/sources/` 的官方固定提交文件清单，共35个文件，包含完整生产源码、头文件、CMake/Kconfig和许可，省略上游CI/示例。下载脚本逐文件检查Git blob SHA1、SHA256及长度，先在临时目录验证后发布；已有目录须内容匹配，不覆盖未知文件。无需携带开发机器的下载包。

新增两项构建期SDK适配位于 `firmware/cmake/`：HID输入资源压力处理和NVS blob中断恢复。它们校验原函数后在构建目录生成副本，不修改外部SDK；原有NimBLE三行补丁仍需按前文配置。SDK升级必须重新核验所有适配。

小智协议参考来源与MIT许可见[小智AI](firmware/xiaozhi_ai.md)。`firmware/components/yumo_ble_store` 为本项目紧凑绑定存储实现，配套回放只用合成身份。
