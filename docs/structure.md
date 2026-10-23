# 目录结构

源码与文档的永久目录为：

```text
firmware/        设备固件
  main/core/     系统服务、硬件服务、后台任务生命周期
  main/ui/       公共 UI、主题、字库和图标
  main/apps/     各应用
  components/    板级适配与项目移植源码
  cmake/         构建期配置生成
server/          Java8 密码本服务（src/main、src/test、pom.xml）
desktop/         投屏客户端、UI 演示
resources/       教学内容源码、SD 卡资源
tooling/         本项目脚本源码和测试夹具
config/          依赖精确版本与内容摘要
patches/         必须保留的依赖适配
packaging/       PC/NAS 部署模板与启动脚本
docs/            使用、架构、构建、部署说明
```

运行后按需生成、但不提交的目录：`dependencies/`（下载组件）、`artifacts/`（构建和测试输出）、`runtime/`（自己的数据库和日志）、`firmware/generated/`（构建头文件）。SDK、JDK、编译器等安装在使用者自行选择的位置。

脚本通过自身位置寻找项目文件，通过环境变量或 PATH 查找外部工具；无需沿用作者盘符、账户名或文件夹。设备的 `/sdcard/` 与 NAS 容器的 `/app`、`/data` 是运行时约定，不是开发电脑路径。
