# 图片与FC兼容范围

更新日期：2026-10-22。FC兼容功能沿用此前已交付实现；渐进式JPEG、Mapper24/VRC6、Mapper74和245均已实现，并纳入当前已烧录固件；此页取代此前的可行性评估，不再将这些功能写成待开发。准确产物见[当前状态](../current-state.md)。

## 图片

2026-10-20当前静态图片上限16MiB，源边长16384像素、源像素64×1024²。JPEG/PNG按可见视口生成不超过240×240的RGB565帧，支持流式解码、渐进系数SD交换及按倍率持久分块缓存。完整预算与缓存目录见[图片解码与缓存](photo_rendering.md)，操作见[相册](reader_photo_random_20260921.md)。

JPEG仅承诺已实现的8位Huffman灰度/RGB/YCbCr路径，不扩展为CMYK、12位、无损或任意编码全支持；PNG保留透明黑底、16位转8位、gamma与Adam7。libjpeg-turbo版本3.1.2，来源与许可见[provenance](../../firmware/components/photo_jpeg/provenance.json)和[许可证](../../firmware/components/photo_jpeg/LICENSE.md)。旧4MiB/整帧解码/禁止SD交换/12秒超时描述已被本次方案取代。GIF独立保留64MiB流式播放，不缩放。

## FC登记和文件限制

当前35种Mapper：`0、1、2、3、4、7、9、10、11、15、16、18、21、22、23、24、25、32、33、34、64、65、66、70、71、73、74、75、78、79、87、93、94、206、245`。

App和C核心共用 [ines.h](../../firmware/components/nofrendo/nes/ines.h)。支持iNES1和基础NES2，submapper必须为0；PRG+CHR最多2MiB，另允许16字节头及512字节trainer。NES2只接受普通大小计数、标准控制台和允许的NTSC/PAL/双区配置，PRG-RAM与CHR-RAM各总计不超过8KiB；混合CHR-ROM/RAM仅放开Mapper74所需的最多2KiB CHR-RAM。指数大小、Dendy、其他扩展、FDS、NSF和压缩包不能直接加载。

### 已完成的兼容扩展

| Mapper | 当前实现及限制 |
| --- | --- |
| 24 / VRC6 | PRG/CHR银行、镜像、指令边界推进的IRQ及两路脉冲/一路锯齿扩展音频；保留现有模拟器时序，非逐总线周期精确模拟。 |
| 74 | MMC3变种，CHR-ROM混合2KiB可写CHR-RAM，支持对应银行选择及存档恢复。 |
| 245 | MMC3变种，外层PRG选择、CHR-RAM半区及MMC3寄存器状态。 |
| 78 | 修正Mapper注册编号，避免编号与入口不一致。 |

MMC3 IRQ沿用已有扫描线渲染模型，并非逐PPU A12边沿模拟。登记某编号不表示其所有游戏、板型或修改版都已实机兼容；遇到问题需核对ROM头、submapper、内容和具体表现。未附商业ROM，不承诺主流游戏全兼容。

FC触控、重力和蓝牙键盘见[控制说明](fc_touch_controls.md)，三个手动槽和独立自动档见[存档说明](fc_saves.md)。手动状态保存包含当时的工作RAM；独立电池SRAM自动持久化尚未实现。

## 来源和验证

Nofrendo固定上游及本地改动集中在[移植说明](../../firmware/components/nofrendo/PORT.md)。VRC6已有105项原生断言；MMC3扩展收据（历史开发记录未随源码分发）记录60,949项合成核心检查；存档收据（历史开发记录未随源码分发）记录23,640项检查，覆盖全部35种登记Mapper的状态回放/重开和五槽文件流程。

用户已表示此前FC改造效果良好并选择保留，但未提供逐ROM、扩展音频、长期帧率或存档掉电验收记录。上述测试数字是已存在收据，本轮整理未重新编译或运行测试。

FC显示已于2026-10-22更新为240×240居中1:1输出，并保留避免交替精灵长期漏显的相位调度；当前Mapper集合不变。《加纳战机》及其他ROM的完整画面仍需实机确认，详见[显示方案与边界](fc_rendering.md)。
