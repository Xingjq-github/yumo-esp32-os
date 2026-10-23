# 项目脚本源码

本目录不包含编译器或运行环境。`firmware/` 负责固件构建、源码检查、模型和资源生成；`server/` 负责 Java 构建、测试与 NAS 打包。

`environment.py` 从 PATH 与环境变量查找用户安装的工具；`configure_environment.py` 只验证环境，不修改外部 Python。`init_config.py` 初始化本副本身份；`prepare_dependencies.py` 下载并校验固定组件，`apply_sdk_patch.py` 应用已记录的 SDK 适配。

日常入口为根目录 `Build.cmd`、`Build-Server.cmd`。测试夹具与 native 回放源码是验证资料，不是需要复制到 SD 卡的资源。
