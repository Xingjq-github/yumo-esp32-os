# JYVAULT2：简化密码本协议

JYVAULT1及旧主密码/授权镜像流程已废止。本部署稳定的存储AES-256密钥、Bearer令牌、vault_id和salt保存在deployment/local-keys.json，并由tooling/init_config.py维护设备vault_builtin.hpp；已有值不重建。2026-09-23新增独立的编译内置同步密钥，文件格式不变，但HTTP鉴权要求已更新，不能把旧JAR当成兼容服务。

## 同步鉴权（与存储密钥分离）

32字节同步密钥从 `firmware/vault-sync-key.properties` 与 `server/vault-sync-key.properties` 编入设备和服务。双端配置必须一致；v2.0与旧目录使用不同同步密钥。证明值为：

```text
lowercase_hex(SHA256(ASCII("JYVAULT-DEPLOY-KEY-V2") || raw_32_byte_sync_key))
```

拼接原始密钥字节而非hex字符串；域串不含终止NUL，C++用 `sizeof(domain)-1`，不要硬编码或猜测长度。每次设备请求同时携带原 `Authorization: Bearer ...` 和 `X-Vault-Key`（64个小写hex字符）。令牌错误返回401；编译证明缺失或不匹配返回403。`manifest`与`bundle`均强制验证；成功bundle为200并包含 `X-Vault-Key` 响应头。设备拒绝缺失、重复或不匹配的证明，并在接受正文前终止。

`DeploymentKeys`不从JSON读取同步密钥，配置不能禁用鉴权；`configure_vault.py`不生成或覆盖它。轮换只修改上述两个源码文件、重新构建并配套部署/烧录。存储key、vault_id、salt、原Bearer令牌和PIN格式保持原值；同步密钥不进入JYVAULT2/JYCONFIG2加解密路径，因此单独轮换它不影响已有密码读取。手工变更存储身份不在此保证内。

## 文件格式

128字节头，所有整数小端：

| 偏移 | 长度 | 内容 |
|---|---:|---|
| 0 | 8 | ASCII JYVAULT2 |
| 8 | 4 | 0（内置密钥，不做主密码KDF） |
| 12 | 4 | 后续密文长度，含16字节tag |
| 16 | 4 | revision，设备空缓存为0，服务器首版为1 |
| 20 | 4 | 保留0 |
| 24 | 16 | 固定vault_id |
| 40 | 16 | 固定部署salt/身份标记 |
| 56 | 12 | 随机密钥封装nonce |
| 68 | 48 | AES-GCM封装随机32字节数据key，AAD为头0..67 |
| 116 | 12 | 随机数据nonce |
| 128 | 可变 | AES-GCM加密UTF-8 JSON，AAD为完整128字节头 |

整个文件144～262144字节，最多256条。payload为schema=2、entries数组。每项含id（32位小写hex）、title、category（work/app/web/other）、username、password、target、note、favorite。可打印ASCII账号/密码各最长128字节；名称96、用途192、单条备注256个UTF-8字节。服务端新增/合并备注超限时按字符边界截去尾部，其他字段不截断；详见[用户说明](../firmware/password_vault.md)。

设备私有配置schema=2，包含url、pin_salt和pin_hash。PIN仅4～12位数字，16字节随机salt，PBKDF2-HMAC-SHA256 10000轮/32字节；校验任务可取消和让出CPU。配置由同一内置key做AES-GCM加密，nonce12字节置于密文前；AAD为ASCII JYCONFIG2加vault_id和salt（共41字节）。PIN不是传输密钥，修改服务器管理员密码不会改变设备缓存密钥。

## 存储和状态

设备vault分区仍为0xEC0000 / 1MiB，双512KiB槽，JVSLOT1提交头和SHA256不变。写非当前槽→内容校验→最后提交头；失败保留当前有效库。无有效槽且并非全FF时报告无效并保留原数据，不自动格式化，App没有重置/整区擦除入口。

每次手动同步用服务端完整有效列表覆盖设备，包括删除和空列表；允许相同revision不同内容和较低revision，不做版本防回退。本地槽generation决定存储新旧。只有完整加密文件字节和地址均相同才省略重复Flash写入；身份不符、认证/GCM、格式、下载或存储失败仍拒绝并保留旧库。设备同步与服务端导入的去重合并不同，见[同步规则](../firmware/vault_sync_20260920.md)。

服务器SQLite仅存一份加密快照，事务保存并递增版本，synchronous=FULL，DELETE日志模式。单用户并发网页通过revision拒绝覆盖冲突，该校验不因设备同步取消版本限制而移除。管理员使用PBKDF2-HMAC-SHA256 100000轮，与设备PIN和部署密钥独立。

## HTTP API

- GET /api/status：首次设置状态。
- POST /api/setup：password和confirm，只有无管理员时可用。
- POST /api/login、/api/logout：内存30分钟会话，HttpOnly/SameSite cookie。
- POST /api/admin/password：验证原管理员密码及两遍新密码，修改后撤销其他会话。
- GET /api/super-mode：当前会话超管状态；POST同路径验证管理员后开启，或退出超管。
- GET /api/entries：普通模式工作/应用/网页只返回不含password的元数据；other返回作为码值的password并移除username。超管可返回普通密码。
- POST /api/entries：创建/编辑，revision并发检查，编辑password空串表示保留。
- POST /api/entries/{id}/delete：revision并发检查。
- POST /api/entries/{id}/reveal：普通模式需会话和再次验证管理员密码；超管已授权会话可直接返回该条密码。
- POST /api/entries/import：网页专用CSV预览/确认，跳过无账号或不支持字符行，按账号密码去重、账号排序，确认仍检查revision。
- POST /api/archive/export、/api/archive/import：全分类加密导出及去重合并导入；预览普通模式隐藏密码，超管可明文。
- GET /api/device/manifest、/api/device/bundle：原Bearer令牌及编译内置同步证明同时验证，仅下载；成功bundle返回匹配证明。

网页写操作校验配置Origin；请求大小、耗时、登录频率、文件长度和并发均有界。HTTP不提供网络机密性：管理密码及reveal响应在局域网明文传输；仅同步文件保持加密。内置密钥不防物理提取，不能将本协议描述为公网密码管理器或安全审计通过的方案。

`X-Vault-Key`是可重放的固定证明，不是挑战响应或TLS服务器认证。设备软件AES-GCM实现保持上述字节格式，认证失败先清输出，长数据协作调度；合成向量一致性测试不等于独立密码学审计。详细排错及交付边界见[同步说明](../firmware/vault_sync_20260920.md)。

2026-10-19分类语义更新不改变同步文件schema：other的username兼容字段保留，旧条目普通编辑不清除；界面不显示账号，password按码值持续显示。分组选择对应原target字段，可选已有值或新值。
