# 环境、初始化、编译与烧录

以下以 Windows 10/11 x64 的 CMD 为验证入口，命令均从仓库根目录执行。工具安装到自己选择的位置，不需要和项目放在同一盘；不要复制旧工程的虚拟环境或 CMake 缓存。源码路径建议使用较短的英文目录。

## 1. 自行安装工具

| 工具 | 本项目使用的版本/用途 |
| --- | --- |
| Git | 获取 ESP-IDF 及其子模块 |
| ESP-IDF | 固定 `v6.1`，见 `config/dependencies.json` 和 [依赖说明](dependencies.md) |
| Python | 3.11，使用 ESP-IDF 安装/激活后的虚拟环境；本项目脚本要求至少 3.11 |
| Xtensa GCC | `esp-15.2.0_20251204`，通过 ESP-IDF 工具安装器安装 |
| CMake / Ninja | 4.0.3 / 1.12.1，通过 ESP-IDF 工具安装器安装 |
| JDK | Temurin/OpenJDK 8，配置 `JAVA_HOME` 并把其 `bin` 加入 PATH；也用于验证教学资源 |
| Maven | 3.9.x，把 `bin` 加入 PATH；首次构建需要联网下载 `pom.xml` 中固定依赖 |
| 可选宿主工具 | GCC/G++（例如 w64devkit），用于额外原生回放；不参与 ESP32 链接 |

仓库不提供上述工具或二进制安装包。安装入口：ESP-IDF 官方文档、Eclipse Adoptium、Apache Maven 官方发布页。不要用未固定版本的最新 SDK 替换既定版本。

例如把 SDK 放到仓库的同级目录，首次执行：

```bat
git clone --branch v6.1 --recursive https://github.com/espressif/esp-idf.git ..\esp-idf
call ..\esp-idf\install.bat esp32s3
call ..\esp-idf\export.bat
python --version
cmake --version
ninja --version
java -version
mvn -version
```

每次新开 CMD，先执行该 SDK 的 `export.bat`。如果 SDK 安装在别处，使用自己安装位置的激活入口；不用修改项目源码。标准环境提供 `IDF_PATH`、`IDF_TOOLS_PATH` 和工具 PATH。

## 2. 准备依赖与配置

```bat
python -B tooling\apply_sdk_patch.py
python -B tooling\prepare_dependencies.py
python -B tooling\init_config.py --url http://vault.local:8443
```

SDK 补丁命令验证并应用本项目的三行 NimBLE 生命周期修复。依赖准备命令把 `config/dependencies.json` 中的 23 个精确版本下载到忽略的 `dependencies/managed_components/`，校验发布文件摘要并应用 RTC 构建适配。再次运行会校验现有内容，不会静默覆盖被修改的依赖。无需安装原工程里的其他未使用组件。

初始化命令生成匹配的双端密钥、存储身份和固件连接头。`vault.local` 是可自行替换的示例主机名，必须能被设备所在局域网解析；最直接的做法是填写自己的服务器 LAN IP。也可随后通过设备密码本的连接设置或 SD 卡配置修改同步地址。详见 [配置说明](configuration.md)。

## 3. 设备固件

```bat
Build.cmd
```

完整入口依次完成配置、生成/校验教学资源、固件编译、源码契约检查和交叉编译器模型检查。资源生成需用到 JDK8；生成的教学包可直接复制到 SD 卡。Ninja 默认 `-j2`。

输出：`artifacts/firmware/01_factory.bin` 是应用程序，不是可以从地址零写入的整片镜像。分区表、bootloader 和 otadata 均在同一构建目录；检查记录位于 `artifacts/validation/`。

```bat
Build.cmd --stage configure
Build.cmd --stage build
Build.cmd --stage checks
Build.cmd --stage models
```

单独运行某一阶段不代表其余阶段通过。`--stage checks` 需已有完整构建及资源，不能在空构建目录中直接运行。移动工程后，删除 `artifacts/firmware/` 再构建，入口会拒绝使用旧位置的缓存。

## 4. 手动烧录

先在系统设备管理器确认自己的串口，用实际端口替换下例的 `COM_PORT`，并确认硬件型号、16 MB Flash、分区布局和板级接线吻合。

首次烧录可使用官方入口，读取本次构建的烧录清单：

```bat
python "%IDF_PATH%\tools\idf.py" -C firmware -B artifacts\firmware -p COM_PORT flash
```

已有雨墨设备、需要检查原分区数据保持不变时，可使用项目的校验入口，把 `BUILD_RUN` 替换为本次成功完整构建的记录目录名：

```bat
python tooling\firmware\flash_firmware.py --port COM_PORT --build-run artifacts\validation\BUILD_RUN --output artifacts\validation\flash-new
```

后者要求设备当前分区表与本工程一致；新空白设备请用前一个入口。正常烧录只涉及 bootloader、分区表、otadata、factory，不要执行整片擦除或将应用镜像写到地址零。

本工程新初始化的存储身份与其他部署不同。已有密码数据的设备不能在保留数据的前提下随意更换存储身份；先按 [配置说明](configuration.md) 决定是否沿用自己的配套身份。烧录本身不代表新旧身份兼容。

## 便携系统升级包

需要分发已有设备的仅系统分区升级包时，先在本副本完成独立初始化及完整构建，再在已激活的Windows x64 Python 3.11环境中使用已安装的esptool 5.4.0运行：

```bat
python -B tooling/firmware/package_portable_flash.py --build-run artifacts/validation/BUILD_RUN
```

`BUILD_RUN` 必须是本副本完整构建成功、所有输入和当前镜像匹配的记录目录。打包器从当前Python环境收集运行时和依赖，不下载或引用开发者机器的目录。本轮仅同步脚本并通过32项烧录测试和15项适配检查，没有在final独立完整编译或生成final身份的固件包。使用方式与保留数据边界见[便携升级](firmware/portable_flash.md)。

## 5. 服务端及 NAS 打包

服务端单独构建只需 Python 初始化配置、JDK8 和 Maven，不需要 ESP-IDF：

```bat
python tooling\init_config.py
Build-Server.cmd
```

生成 `artifacts/server/joyvault.jar`。默认联网解析 Maven 依赖，下载在 Maven 本机缓存；可用 `Build-Server.cmd -Offline` 复用已经完整准备的缓存。

先执行隔离 HTTP/协议测试，再生成 NAS 介质：

```bat
python tooling\server\java_integration_test.py --java java --jar artifacts/server/joyvault.jar --work artifacts/validation/server-http
python tooling\server\package_nas.py --verification artifacts/validation/server-http/result.json
```

本节对应仓库提供的独立密码服务；源码库不附带可直接部署的NAS包。

测试目录要求不存在；重复测试换一个新目录名。测试只启动隔离临时服务，结束后关闭。打包检查本次 JAR 哈希、JUnit 和 HTTP 结果，输出到 `artifacts/releases/nas/`。介质含本部署身份及嵌入同步密钥，不要将自己的介质公开提交到源码仓库。

PC 与 NAS 启动分别见 [PC 服务](server/pc.md) 和 [NAS 图形部署](server/nas.md)。这些都是手动操作；构建不会启动正式服务。

## 6. SD 卡与桌面功能

把 `resources/sdcard/` 的内容复制到 SD 卡根目录，按其 README 放置自己的文件；不用再套一层项目名目录。知识包可随仓库提供的资源使用，也可由完整构建重新生成。FC 游戏需自行提供合法 ROM。

USB 投屏：安装 `desktop/pc-display/requirements.txt` 中依赖，使用带 Tkinter 的 Python 运行 `desktop/pc-display/receiver.py`，在设备设置开启 USB 投屏后手动连接串口。HTML 演示直接打开 `desktop/ui-preview/index.html`。

## 7. 清理与提交

构建后可以删除 `artifacts/`、Python 的 `__pycache__/` 和下载的 `dependencies/`；下一次构建重新准备依赖。不要误删 `runtime/` 中自己需要的数据库，也不要重建正在使用的存储身份。

`.gitignore` 已排除依赖、输出、运行数据和本地密钥。提交前核对暂存列表；保留项目脚本、测试夹具、CMake、POM、锁定清单、补丁和资源。配置示例可以提交，真实初始化文件不提交。

## 已烧录来源与额外宿主回放

当前功能基线来自2026-11-02成功烧录版本，详情见[当前状态](current-state.md)。本仓库保留独立初始化；仍须按上文构建自己的镜像，不能把来源镜像摘要当成本地新构建结果。

可选安装 GCC/G++ 后运行电量、Wi-Fi 与局域网生产逻辑回放。将 `GXX_PATH` 替换为实际 g++ 可执行文件路径，每次使用新的 run-dir：

```bat
python -B tooling\firmware\test_battery_estimator_runtime.py --compiler GXX_PATH --run-dir artifacts\validation\battery-local
python -B tooling\firmware\test_wifi_lifecycle_runtime.py --compiler GXX_PATH --run-dir artifacts\validation\wifi-local
python -B tooling\firmware\test_lan_share_runtime.py --compiler GXX_PATH --run-dir artifacts\validation\lan-local
```

这些检查只编译宿主回放程序，不访问串口，也不能替代 ESP32 完整编译、真实无线转发或长期电池/走时验收。RTC 配置检查已加入模型源码，完整 Build.cmd 会自动发现。

### 时钟自学习与测速回放

先把宿主 `g++` 所在目录加入 PATH。以下时钟、网络与系统回放只使用生产代码和替身，不需要把 SDK 或第三方依赖复制进仓库：

```bat
python -B tooling\firmware\test_clock_drift_runtime.py --run-dir artifacts\validation\clock-local
python -B tooling\firmware\test_network_time_runtime.py --run-dir artifacts\validation\network-time-local
python -B tooling\firmware\test_system_audit_runtime.py --run-dir artifacts\validation\system-local
python -B tooling\firmware\test_speed_test_http.py --compiler GXX_PATH --run-dir artifacts\validation\speed-http-local
python -B tooling\firmware\test_speed_test_measure.py --compiler GXX_PATH --run-dir artifacts\validation\speed-measure-local
python -B tooling\firmware\test_speed_test_quality.py --compiler GXX_PATH --run-dir artifacts\validation\speed-quality-local
```

SDK 边界契约 `python -B tooling\firmware\check_clock_drift.py` 使用 `IDF_PATH` 指向固定 ESP-IDF v6.1，需先激活环境；没有设置时明确失败，不跳过 SDK 断言。它核验深睡与 SNTP 的实际入口及回调顺序，不代表完整链接验证。

测速存储/UI 回放另外需要按准备脚本安装 cJSON/LVGL，完整源码检查还依赖本目录构建和生成资源。本轮没有为源码同步下载依赖或重新构建 ESP32 镜像。现有身份快照契约等历史问题见[验证说明](validation.md)，不要通过覆盖身份或扩大45项排除来绕过。
