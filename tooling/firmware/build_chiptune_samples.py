"""Create original chip-score arrangements, without external audio assets."""
from pathlib import Path
import json,re
from package_sd_resources import write_chiptune_archive
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
OUT=ROOT/'../resources/sdcard/chiptune'
def beats(sequence):
    default=4;total=0;pos=0;text=re.sub(r'\s+','',sequence)
    token=re.compile(r'([OL])(\d+)|([A-GRKSH])[#-]?(\d*)(\.)?|[<>]')
    while pos<len(text):
        m=token.match(text,pos)
        if not m:raise ValueError('Invalid sample note at '+str(pos))
        pos=m.end()
        if m[1]=='L':default=int(m[2])
        elif m[3]:
            duration=1920//int(m[4] or default)
            total+=duration*3//2 if m[5] else duration
    return total
def score(title,tempo,lead,bass,repeat=2,drums=True):
    # Sample melodies use only plain durations; dotted syntax remains available
    # to user-authored files and is independently exercised by native tests.
    lead=' '.join([lead]*repeat);bass=' '.join([bass]*repeat)
    ticks=max(beats(lead),beats(bass))
    tracks=[dict(id='lead',wave='pulse',duty=.5,volume=.62,gate=.88,notes=lead),
            dict(id='bass',wave='triangle',volume=.55,gate=.85,notes=bass)]
    if drums:
        n=ticks//240
        tracks.append(dict(id='drums',wave='percussion',volume=.22,notes='L8 '+' '.join('K H S H K H S H'.split()[i%8] for i in range(n))))
    return dict(format='chip-score',version=1,title=title,tempo=tempo,loop=True,tracks=tracks)
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    songs=[
      ('01_canon','卡农·像素变奏',96,
       'O5 L4 F# E D C# O4 B A B O5 C# D C# O4 B A G F# G E O5 L8 D F# A F# E C# A E F# D B F# E C# A E O4 B G O5 D O4 B A F# O5 D O4 A B G O5 D O4 B O5 C# E A E',
       'O3 L2 D O2 A B F# G O3 D O2 G A O3 D O2 A B F# G O3 D O2 G A',2,False),
      ('02_ode','欢乐颂·方波',112,
       'O5 L4 E E F G G F E D C C D E E4 D8 D8 D2 E E F G G F E D C C D E D4 C8 C8 C2',
       'O3 L2 C G C G F C G G C G C G F G C C',2,True),
      ('03_twinkle','小星星·芯片夜空',108,
       'O5 L4 C C G G A A G2 F F E E D D C2 G G F F E E D2 G G F F E E D2 C C G G A A G2 F F E E D D C2',
       'O3 L2 C C F C F C G C C G C G C G C G C C F C F G C C',2,False),
      ('04_korobeiniki','货郎·民谣主题',132,
       'O5 L8 E4 O4 B O5 C D4 C O4 B A4 A O5 C E4 D C O4 B4 B O5 C D4 E4 C4 O4 A4 A2 O5 D4 F A4 G F E4 C E4 D C O4 B4 B O5 C D4 E4 C4 O4 A4 A2',
       'O3 L2 A E A E A E A E D A C G E B A A',2,True),
      ('05_pixel_adventure','雨墨·像素冒险',138,
       'O5 L8 C E G C E G A G E C D E G E D C D F A D F A B A F D E F A F E D E G B E G B O6 C O5 B G E F G B G F E O5 D G B O6 D C O5 A F D E G C E D B O4 G B',
       'O3 L2 C G C G D A D A E B E B G D F G',2,True),
      ('06_forest','雨墨·森林散步',88,
       'O5 L8 E G A G E D C D E G B A G E D4 D F A G F D C D F A O6 C O5 B A G F4 C E G E D E A G E C D E G E C4 D G B G E G O6 C O5 A G E D B O4 G2',
       'O3 L2 C G C G D A D A F C F C G D G C',2,False),
      ('07_arcade','雨墨·街机冲刺',156,
       'O5 L8 A C O6 E O5 A C O6 E D C O5 B G D B G D F G A C O6 E O5 A C O6 E G E D O5 B G D E G A4 O5 L8 A B O6 C E G E C O5 B A G E C D E G A B O6 D F D O5 B A G F E D C O4 B O5 E4 A4',
       'O3 L2 A E G D A E G A F C D A G D E A',2,True),
    ]
    manifest=[]
    for name,title,tempo,lead,bass,repeat,drums in songs:
        data=score(title,tempo,lead,bass,repeat,drums)
        (OUT/(name+'.chip.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        manifest.append(dict(file=name+'.chip.json',title=title,source='original' if name[:2]>='05' else 'public-domain composition; newly arranged'))
    (OUT/'曲目与使用说明.txt').write_text('''8Bit音乐资源包

复制方法：把整个chiptune目录复制到SD卡根目录。设备打开“8Bit音乐”，点击刷新。
只扫描本目录第一层的 *.chip.json（不区分扩展名大小写），按文件名排序，最多200首。
为避免巨大目录拖慢设备，最多检查4096个目录项；超出任一限制时页码旁显示“已截断”。
首次进入不播放。点击曲目开始，播放页支持暂停/继续、停止、上下曲、音量和单曲循环。
返回列表仍播放；退出APP停止并释放资源。普通熄屏仍播放。切歌和刷新需要SD卡。
刷新会停止当前曲目并重新检查目录；文件有错会显示“!”。点击条目查看具体原因。
所有示例初始开启循环，可在播放页关闭。

格式：UTF-8 JSON，format=chip-score，version=1，tempo=40~240。
最多2条pulse、1条triangle、1条percussion。每首最多64KiB、2048个事件、10分钟。
音符：C D E F G A B；#升半音、-降半音；R休止；O2~O6八度；>升/<降八度。
L1/L2/L4/L8/L16/L32设置默认时值，音符后数字覆盖时值，单附点延长一半。
例：O5 L8 C E G E R4。打击轨仅K底鼓、S军鼓、H踩镲、R、L和时值。
不同音轨同时起奏，较短轨结束后静音等待整首结束。
不支持MP3、MIDI、游戏ROM或任意其他MML方言；不能直接改扩展名使用。

曲目来源与范围：
01 卡农：Johann Pachelbel（1653–1706）的公版作品主题，本项目重新编曲。
02 欢乐颂：Ludwig van Beethoven（1770–1827）的公版作品主题，本项目重新编曲。
03 小星星：传统法国民谣 Ah! vous dirai-je, maman 的公版旋律，本项目重新编曲。
04 货郎：传统俄罗斯民谣 Korobeiniki 的公版旋律，本项目重新编曲；不是游戏原声录音或其特定编曲。
05~07 为本项目新创作的短曲，不是现有游戏或动漫主题曲。
没有复制商业录音、现代出版乐谱或受保护的游戏/宫崎骏动漫主题曲。
这些JSON是自行生成的简化芯片风格编曲，不宣称忠实复刻任何原声或硬件音频芯片。
新增编曲及原创内容允许用户复制、修改、使用和再分发，无需向本项目支付费用。
设备喇叭音色及响度仍需实际试听；电脑验证只核对协议、节奏和PCM输出。
''',encoding='utf-8')
    archive=ROOT/'../artifacts/releases/sd/chiptune-samples.zip'
    write_chiptune_archive(OUT.parent,archive)
    print(json.dumps(manifest,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
