# 当前功能基线

更新日期：2026-11-03。设备功能源码来自2026-11-02 10:48:45（北京时间）成功烧录版本，目标板为ESP32-S3-Touch-LCD-1.54。应用清单见[25个应用概览](firmware/overview.md)，环境准备见[构建说明](build.md)。

## 本次更新

- [小智AI](firmware/xiaozhi_ai.md)：Wi-Fi在线手动语音、官方绑定及识别/回答显示；36px顶部、可滚动正文和44px主按钮适配240×240屏幕与5套主题。worker工作栈由8KiB扩至48KiB PSRAM，退出释放。
- [蓝牙键鼠](firmware/bluetooth_connections.md)：触控提交与发送解耦，NimBLE队列分包、回压和连接代次隔离，压低高频通知日志。安全绑定16条、CCCD128条、名称/角色16条，旧8条数据迁移，同时仍仅一个连接。
- [密码本](firmware/password_vault.md)：全屏九点图形输入、60px半透明外圈、异步验证、分类与二维码；[HTTP/HTTPS同步](firmware/vault_public_https_20261031.md)支持域名、证书验证及公网端口映射。
- [Wi-Fi连接](firmware/wifi_connection_latency.md)：首次有效信道采用FAST扫描，失败回退ALL扫描，增加阶段耗时诊断。
- [显示保护](firmware/screen_protection.md)：应用独立不透明页面和120秒静置保护，用于缓解LCD残像，不承诺消除已有残影。
- 编程知识包449条，含Kubernetes/K9s；[SD资源](../resources/sdcard/README.md)需另行更新，保留自己的收藏与阅读状态。
- [便携系统升级脚本](firmware/portable_flash.md)：自动识别USB串口，默认仅更新factory并核验其他区域；仓库只提供脚本和模板。

## 保留能力

MP3/LRC本机扬声器播放、FC居中1:1像素、IINE C模式游戏/H模式导航、图片16MiB输入及SD视口缓存、秒表动态分段、萌芽物语、Wi-Fi共享与文件硬盘、测速图表和SD历史、RTC联网自学习、多主题及统一四向旋转继续保留。

USB串口、投屏和读卡器保留；USB有线键盘、Type-C耳机和蓝牙耳机方案未纳入。图片解码仍受像素、内存及预算限制；FC显示调度上限不能视为所有ROM实测帧率。

## 来源与验证边界

来源镜像6,389,808字节，SHA256为`724ab2aa687e52edd23c966ba6ce0d2548c090113aa272f8fb37d90391a86643`；factory余量3,375,056字节。此次设备升级只写0x20000处factory，写后校验及40秒启动观察通过，NVS/PHY/vault及SD未改动。

公开副本保留独立初始化与外部工具链适配，不携带部署密钥或固件。511项公开构建输入与已烧录来源一致，另核对5项CMake文件；自己的初始化身份会产生不同二进制。11月3日同步未重新编译完整ESP32固件或烧录设备。

小智真实云端对话、栈修正后首次真实Opus编码与音质、蓝牙鼠标长期体验、Wi-Fi提速、公网密码同步及完整硬件兼容性仍需实机验收。已运行检查及限制见[验证说明](validation.md)。
