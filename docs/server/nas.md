# 雨墨密码本：绿联 DX4600 Pro 图形界面部署（Java 8）

本介质使用你提供的 `eclipse-temurin:8-jre` 镜像，运行可执行的 `joyvault.jar`。无需在 NAS 安装 JDK、Maven、Python，也无需构建镜像。JYVAULT2 协议保持不变；首次克隆须生成自己的双端身份。HTTP同步鉴权已增加编译内置证明，必须使用与设备配套的新版JAR。

适用于绿联云 Docker 的容器创建界面。UGOS 更新后标签名称可能略有变化，以下按配置项含义填写；未远程操作你的 NAS，不假定某个具体 UGOS 版本或磁盘路径。

## 本工程的介质生成方式

此文档对应本仓库提供的独立密码服务。

部署模板保留在 `packaging/nas`。`artifacts/releases/nas/` 是打包输出位置，源码仓库不附带预制部署包。需要部署这套独立服务时，按[构建说明](../build.md)重新构建JAR、执行隔离HTTP验证，再用 `tooling/server/package_nas.py --verification <验证结果JSON>` 打包。

新包的JAR哈希和验证范围由包内 `verification.json`、`SHA256SUMS` 与ZIP旁 `.sha256` 核验。本机生成JAR位于 `artifacts/server/joyvault.jar`，该独立服务的运行数据位于 `runtime/server/data`。包内包含JAR、配套身份、说明和Compose模板，不含Docker镜像、数据库或测试数据；升级保留原身份与数据目录。

## 1. 在文件管理中准备目录

把重新构建并校验通过的 `joyvault-nas-java8-YYYYMMDD.zip` 上传到NAS，解压到新建的 `Docker/joyvault-app`；再创建同级 `Docker/joyvault-data`。两个目录名称只是示例，以NAS实际位置为准。压缩包根目录直接放置JAR，挂载时选择能直接看到JAR的那一层。

```text
Docker/
  joyvault-app/
    joyvault.jar
    deployment/local-keys.json
    README.md                   可选说明
    compose.yaml / .env.example  仅Compose方式需要
    verification.json           本次介质的验证范围
    SHA256SUMS                  包内各文件SHA256
  joyvault-data/                数据持久化目录，初次使用可以为空
```

`joyvault-app` 必须直接包含JAR，不要误选多套的一层目录。`deployment/local-keys.json`是本部署存储身份及Bearer，应长期保留；同步密钥已经编入当前JAR，不需要新增环境变量或JSON字段，不能靠复制JSON补齐旧JAR的鉴权。不要把运行数据或日志作为应用文件夹上传；已有数据按下文单独迁入持久目录。

需要沿用电脑上现有密码时，在电脑与 NAS 服务均停止时，将现用 `runtime/server/data` 的内容复制进 `joyvault-data`，并保持本包同一套密钥；也可在空库初始化后通过网页导入加密导出文件。不要用旧 Python 数据库覆盖 Java 库。只运行一个实例访问同一个数据目录。

## 2. 在 Docker 的镜像页面创建容器

打开绿联云的 Docker 应用，在本地镜像中找到 `eclipse-temurin:8-jre`，选择创建/运行容器。尚未下载时在镜像仓库下载完整名称 `eclipse-temurin:8-jre`。使用 DX4600 Pro 对应的 Linux amd64 镜像，勿选其他大版本。

基本参数：

| 图形配置项 | 填写内容 |
| --- | --- |
| 容器名称 | `joyvault` |
| 镜像 | `eclipse-temurin:8-jre` |
| 网络模式 | `bridge`，不需要 host 网络 |
| 重启策略 | `unless-stopped`（除非手动停止，否则重启）；若界面只提供自动重启开关则开启，并以实际策略为准 |
| 内存上限 | `512 MB/MiB` |
| CPU 上限 | 有此项时设为 `1` 核；不要设为 1% |
| 工作目录 | `/app`（如果界面提供） |
| 用户/UID | 留空，使用镜像默认用户 |
| 特权模式 | 关闭 |
| 交互/TTY | 不需要开启 |
| 根文件系统只读 | 普通容器向导保持默认；若开启则必须另提供可写且可执行的 `/tmp`，见可选 Compose 方式 |

内存 512 是容器上限，不是程序固定占用。镜像默认用户避免图形部署额外依赖数字 UID 和命令行权限设置；不要额外填写旧说明中的 `10001:10001`。

## 3. 存储、端口与环境变量

在“存储空间/文件夹映射/挂载”一类配置中添加两个**文件夹绑定挂载**：

| NAS 选择的实际文件夹 | 容器路径 | 权限 |
| --- | --- | --- |
| 解压后的 `joyvault-app` | `/app` | 只读 |
| 专门创建的 `joyvault-data` | `/data` | 读写 |

密码数据库会生成在 NAS 的 `joyvault-data/vault.db` 中；停止、删除或重建容器都不会主动删除这个绑定目录。重新创建容器时选择原目录即可。数据目录放在 NAS 本地硬盘，不要放在 SMB/NFS 网络挂载里。NAS 文件夹权限应允许容器读取应用目录、写入数据目录。

端口映射添加一行：

| 本地/NAS 端口 | 容器端口 | 协议 |
| --- | --- | --- |
| `8443` | `8443` | TCP |

下面用 `NAS_LAN_IP` 举例，**改成你的 NAS 局域网 IP**。逐行新增环境变量：

| 名称 | 值 |
| --- | --- |
| `VAULT_PUBLIC_URL` | `http://NAS_LAN_IP:8443` |
| `VAULT_BIND_IP` | `0.0.0.0` |
| `VAULT_DATA_DIR` | `/data` |
| `VAULT_KEYS_FILE` | `/app/deployment/local-keys.json` |
| `VAULT_HEALTH_URL` | `http://127.0.0.1:8443` |
| `VAULT_LOG_FILE` | `/data/logs/vault.log` |
| `TZ` | `Asia/Shanghai` |

注意：`VAULT_BIND_IP` 在容器内填写 `0.0.0.0`，不能填写 NAS 的 IP；对外地址只填在 `VAULT_PUBLIC_URL`。这里 `8443` 实际提供 **HTTP**，不是 HTTPS。浏览器必须使用配置中的相同 IP 和端口，别换成另一个域名，否则网页的 Origin 校验会拒绝写操作。

如果 NAS 的 8443 已被占用，可以统一改为 18443：本地端口、容器端口、`VAULT_PUBLIC_URL` 和 `VAULT_HEALTH_URL` 的端口都改成 18443。本程序从公开地址取监听端口，不能只改单侧映射。

## 4. 填写启动命令并启动

找到“命令/运行命令/CMD”字段，将下面内容作为**一整行**粘贴：

```text
java -Xms32m -Xmx192m -Xss512k -XX:MaxMetaspaceSize=96m -XX:ReservedCodeCacheSize=32m -XX:MaxDirectMemorySize=16m -XX:ActiveProcessorCount=1 -XX:+UseSerialGC -XX:+ExitOnOutOfMemoryError -Dfile.encoding=UTF-8 -Djava.io.tmpdir=/tmp -Duser.timezone=Asia/Shanghai -jar /app/joyvault.jar
```

如果同时有“入口点/Entrypoint”和“命令”，保留镜像入口点默认值，上面一行只放在命令字段。官方镜像入口点会执行这条命令。不需要 `nohup`、`&`、`bash` 或启动脚本。

保存并启动容器，等服务启动后，在电脑或手机浏览器访问：

```text
http://NAS_LAN_IP:8443
```

空库首次访问要求设置至少 4 位管理员密码；使用已有 Java 数据库则用原管理员密码。新增一条测试记录，停止容器再启动，确认仍能看到记录，即可确认数据目录映射有效。已有库可以只检查原记录，无需修改密码。

健康状态可直接在浏览器查看：

```text
http://NAS_LAN_IP:8443/healthz
```

正常响应为 `{"status":"ok","protocol":2}`。普通图形向导没有设置健康检查时，Docker 只展示运行状态也正常。应用日志默认写入 `joyvault-data/logs/vault.log`，所以容器控制台日志可能很少；用 NAS 文件管理查看该日志即可。

## 5. 设备连接 NAS

设备手动连接同一局域网 Wi-Fi，在密码本的连接设置中填写上面的 NAS 地址，或修改 SD 卡 `/config/vault.json`：

```json
{"schema":1,"url":"http://NAS_LAN_IP:8443"}
```

只填写服务根地址，不加 `/api`。设备点击同步先用本地地址，失败后才尝试 SD 配置，成功后保存新地址。切换到NAS前检查电脑服务与设备同步地址，避免设备继续从旧电脑地址同步；本次整理没有启停任何服务。建议在路由器给 NAS 保留固定局域网地址。

同步会用服务端完整列表覆盖设备，包括删除记录和空列表。先确认 NAS 网页里有需要的密码，再同步设备。设备无需蓝牙即可同步；若提示同步密钥不匹配或更新服务，先核对实际JAR与设备的编译密钥，不要重生成存储身份、重设PIN或清库。当前本机配套交付已通过服务/启动检查，设备实际同步和NAS运行仍待用户测试。

## 6. 长期运行与升级

服务维持单个 Java 进程、单个 SQLite 连接，不依赖 Redis/MySQL，没有后台同步、自动备份或扫描。初始堆 32MiB、最大堆 192MiB，SerialGC、1 个可用处理器；Tomcat 最多 16 个工作线程、最少 2 个空闲线程、最多 32 个连接。应用日志轮转至约 3MiB，空闲时不会持续记录访问日志。SQLite 仍保留 FULL 同步和事务回滚。

图形界面有 Docker 日志大小设置时设为每份 1MiB、保留 3 份。正常停止等待至少 30 秒，让应用完成最长 20 秒的优雅停机。不要通过定时强制重启或禁用持久化来省电。

JVM/容器上限有助于控制资源；NAS 整机功耗和硬盘休眠还取决于其他应用、存储和请求量。本机短时测试不能证明 NAS 长期稳定，部署后在图形资源监控中观察空闲 CPU、内存是否持续增长及重启次数。

升级时先在图形界面停止容器，替换 `joyvault-app/joyvault.jar`，保留原 `deployment/local-keys.json` 和整个 `joyvault-data`，然后启动。更新镜像也挂回这两个原目录。无需重新生成密钥或初始化数据库。

Java 8 兼容版采用 Spring Boot 2.7.18。Spring Boot 2.x 开源维护已于 2023 年 11 月结束；部分可兼容的基础依赖已单独更新，但这不等于整个框架仍受维护。本方案供你的可信局域网使用，Java 8 镜像仍应使用维护中的更新版本。

## 可选：使用图形“项目/Compose”入口

主要步骤已可完全通过容器向导完成。如 Docker 提供项目/Compose 编辑器，也可以使用包内 `compose.yaml`，它额外配置只读根目录、32MiB 可执行 tmpfs、日志轮转、进程数上限和每 5 分钟一次的健康探针。

1. 将完整解压目录选为项目目录。
2. 把 `.env.example` 另存为 `.env`，填入 NAS IP、端口及 `joyvault-data` 的真实绝对路径；镜像保持 `eclipse-temurin:8-jre`。真实路径从 NAS 文件管理属性中获取，不照抄 `/volume1`。
3. 在图形界面导入/粘贴 `compose.yaml`，确认编辑器支持读取该目录 `.env`，然后创建/启动项目。若不支持 `.env`，在编辑器中将占位符直接替换成实际值。

Compose 的 `.env` 中 `VAULT_BIND_IP` 指 NAS 对外 IP；模板会将容器内部同名变量单独设为 `0.0.0.0`。Compose 按文件单独只读挂载 JAR 和 deployment，效果与上面的整个应用目录只读挂载相同。没有设置非 root UID，不需要 SSH 执行 chown。

`/tmp` 用于 SQLite 解压并加载 JNI 库，必须可写、可执行，不能设为 `noexec`。健康标记 `unhealthy` 本身不会自动重启容器；进程异常退出才由重启策略处理。手动停止后 `unless-stopped` 不会擅自启动。

## 常见问题

| 现象 | 检查方法 |
| --- | --- |
| Unable to access jarfile | `/app` 是否直接包含 `joyvault.jar`，是否多套一层解压目录 |
| UnsupportedClassVersionError | 使用本轮 Java 8 JAR，勿混用旧 Java 17 产物；镜像确认是 `eclipse-temurin:8-jre` |
| 容器立即退出、找不到密钥 | 完整解压 `deployment/local-keys.json`，确认应用目录可读 |
| Cannot open vault | 检查 data 写权限、固定密钥是否匹配、是否第二个实例占用；不要删除数据库处理 |
| SQLite native 加载失败 | 确认 Linux amd64 镜像，`/tmp` 存在且可写/可执行 |
| 网页打不开 | 看容器是否运行、NAS IP、两侧端口、局域网防火墙和 HTTP 协议 |
| 设备同步403 / 提示更新服务 | 换用当前JAR，确认它与固件编译同步密钥一致；原存储身份JSON不能代替新HTTP鉴权 |
| 网页保存时 403 | 浏览器地址与 `VAULT_PUBLIC_URL` 的协议、主机、端口须一致 |
| 看不到控制台日志 | 查看 NAS 持久目录中的 `logs/vault.log` |
| 重启后要求重新初始化 | 检查是否重新挂载了另一个空数据目录，不要继续覆盖原库 |

## 验证与官方依据

包内 `verification.json` 记录对应 JAR 的实际验证结果和哈希；构建与打包步骤见 `docs/build.md`。源码仓库不附带预编译介质，必须先初始化自己的身份并编译。

桌面构建和协议测试不能替代 NAS 实机及长期功耗验证。部署包不含 Docker 镜像或真实密码库，包含自己的配套部署身份，不能当作公开源码附件发布。

官方资料：

```text
https://docs.spring.io/spring-boot/docs/2.7.18/reference/html/getting-started.html
https://spring.io/blog/2023/11/23/spring-boot-2-7-18-available-now
https://github.com/adoptium/containers/blob/main/8/jre/ubuntu/noble/Dockerfile
https://docs.docker.com/engine/storage/bind-mounts/
https://docs.docker.com/reference/compose-file/services/
https://support.ugnas.com/
```
