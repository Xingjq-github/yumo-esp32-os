# 资源

`knowledge/` 是教学内容源，`sdcard/` 是SD卡资源目录。将sdcard目录内需要的内容直接复制到卡根目录；不要额外套一层resources或sdcard。压缩交付包在 `../artifacts/releases/sd/`。

各目录用途、配置方式及设备生成的文件见 [SD 卡目录总说明](sdcard/README.md)。

`sdcard/` 仅保留设备使用的资源、配置和说明文档。Java 教学源码保留在 `knowledge/PatternExamples.java`；生成的可读关卡与目录资料分别位于 `../artifacts/resources/patterns/lessons.json`、`../artifacts/resources/programming/catalog.json`，供电脑端校验使用，不复制到 SD 卡。
