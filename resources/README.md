# 项目资源

`knowledge/` 是教学内容源，`sdcard/` 是当前SD卡资源目录。将 `sdcard` 内需要的子目录直接复制到卡根目录，不要额外套一层 `resources` 或 `sdcard`；详见[SD目录说明](sdcard/README.md)。

历史SD模板和8Bit音乐ZIP已从活动交付目录归档，不再作为资源来源或检查前提。当前配置模板、乐谱、知识包及说明保留在 `sdcard/`，设备上已有的录音、ROM、存档、阅读状态和游戏进度应保留。

需要重新生成单独的传输ZIP时，在项目根目录使用已配置的Python环境执行：

```bat
python -B tooling/firmware/package_sd_resources.py template --output artifacts/releases/sd/yumo-sd-template.zip
python -B tooling/firmware/package_sd_resources.py chiptune --output artifacts/releases/sd/yumo-chiptune-samples.zip
```

上面是生成命令，源码目录不要求这些ZIP预先存在。`template` 只收集两份配置文件并建立 `config/`、`recordings/`、`nes/` 目录；`chiptune` 收集当前8Bit乐谱目录。知识包和其他资源直接按需复制，重新打包不代表已经写入设备SD卡。
