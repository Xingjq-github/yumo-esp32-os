# PC 手动启动密码本服务

准备 JDK8、Maven、Python，按 [构建说明](../build.md) 初始化双端身份并执行 `Build-Server.cmd`。不附带运行时、数据库或自动启动任务。

把 `packaging/pc/local-config.json.example` 复制为同目录下的 `local-config.json`，将 `url` 改为电脑自己的局域网地址和端口。默认示例 `http://127.0.0.1:8443` 只适合本机测试，不能用于设备同步。`java` 留为 `java` 时从 `JAVA_HOME` 或 PATH 查找；需要指定其他安装位置时可填写相对启动脚本的路径。

在根目录运行：

```bat
packaging\pc\Start-Local.cmd
packaging\pc\Open-Local.cmd
packaging\pc\Status-Local.cmd
packaging\pc\Stop-Local.cmd
```

服务手动启动，数据写入 `runtime/server/data`；状态、日志及临时文件位于同级子目录。停止入口通过本实例控制通道请求正常退出，端口冲突时拒绝启动，不会停止其他程序。

首次打开网页设置管理员密码，即可分类管理工作、应用和网页密码。可导入网页 CSV、使用加密导入导出、生成推荐密码、修改管理员密码和开启超管展示。设备与 PC 在可互访的局域网内，设备密码本填写此服务地址后点击同步。

电脑防火墙如拦截连接，应仅按自己的局域网需求允许对应 TCP 端口。网页的访问地址要与配置 `url` 一致。USB 投屏与密码服务相互独立，无需同时运行。
