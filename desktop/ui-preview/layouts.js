/* Independent, offline concept prototypes. Every account/file is synthetic. */
'use strict';
(() => {

const DRAW = new Set(["clock","flashlight","vault","keyboard","mic","music","random","game","text","photo","lan","settings","gravity","touch","wifi","bluetooth","sun","silent","home","grid","next","back","close","play","pause","stop","record","eye","sync","enter","work","globe","folder"]);
const svg=(name,cls='')=>window.DevicePreview.icon('layout',DRAW.has(name)?name:'grid',cls);
const esc = value => String(value).replace(/[&<>"']/g, c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const APPS = [
  ['时钟','clock','时间，抬手即见'],['手电筒','flashlight','点亮眼前的一小片'],['密码本','vault','常用账号，触手可及'],['蓝牙键鼠','keyboard','口袋里的控制台'],
  ['录音机','mic','记录此刻的声音'],['蓝牙音乐','music','给日常加一点节奏'],['随机','random','把选择交给偶然'],['FC游戏','game','像素里的快乐'],
  ['文本','text','慢慢读，随时继续'],['相册','photo','把画面装进口袋'],['局域网','lan','连接身边的设备'],['设置','settings','按照你的习惯'],['重力球','gravity','跟着倾斜动起来'],['触控展示','touch','看见每一次触碰']
];
const CATEGORIES = [['工作','work',3],['应用','grid',4],['网页','globe',5],['其他','folder',2]];
const ACCOUNTS = [['demo@example.com','演示网页','Demo-Only!16'],['studio@example.com','设计工具','Preview-Only#8'],['office-demo','办公账号','Not-Real!2026']];
const CONTROLS = [['wifi','wifi','Wi-Fi'],['bt','bluetooth','蓝牙'],['motion','gravity','重力感应'],['silent','silent','静音'],['brightness','sun','屏幕亮度'],['home','home','返回桌面']];
const CONCEPTS = [
  {id:'tiles',number:'06',name:'拼贴 · 信息磁贴',tag:'DASHBOARD / BENTO',subtitle:'先看到状态，再进入应用',structure:'大小磁贴混排：时间、录音、密码与连接状态各占不同面积。',try:'点击磁贴打开应用；底部切换桌面、全部应用和控制中心。',tradeoff:'常用功能更直接；桌面不能一次展示全部 App。'},
  {id:'orbit',number:'07',name:'轨道 · 径向菜单',tag:'RADIAL / SELECT + CONFIRM',subtitle:'围绕中心选择，而不是上下找列表',structure:'六个环形入口 + 中央确认区；功能随选择聚焦到中心。',try:'点外围选择，点中心执行；左右划动选择，底部箭头换组。',tradeoff:'方向记忆鲜明；选择、执行分两步，密度低于列表。'},
  {id:'focus',number:'08',name:'一页 · 滑动卡片',tag:'CARDS / ONE TASK AT A TIME',subtitle:'一次只看一个重点',structure:'16px 标题、位图图标、分页进度；录音页变成一盒小磁带。',try:'左右划动或点箭头换卡，点击卡片打开；设置也逐项切换。',tradeoff:'触控区大、内容醒目；跨多个应用要多翻几页。'},
  {id:'rail',number:'09',name:'分舱 · 侧栏工作台',tag:'SPLIT VIEW / PERSISTENT RAIL',subtitle:'导航不动，右侧内容切换',structure:'44 px 常驻侧栏 + 196 px 内容区；密码采用分组展开。',try:'点击左侧在桌面、密码、录音、设置之间切换；右侧独立滚动。',tradeoff:'跨应用切换直接；长账号可用宽度比全屏列表小。'},
  {id:'terminal',number:'10',name:'游标 · 复古终端',tag:'INDEX / HARD-KEY FRIENDLY',subtitle:'用编号、光标和确认键组织操作',structure:'五行编号列表、反白游标、固定方向/确认区；无圆角卡片。',try:'点行选中，再点确认；也可点屏幕后用电脑 ↑ ↓ / Enter 操作。',tradeoff:'单指按钮即可操作；不是所有项目都能一触即开。'},
  {id:'tabs',number:'11',name:'索引 · 标签抽屉',subtitle:'先选分类，再点应用；紫色文件夹式层级',try:'顶部切换日常、工具、媒体，下面整行可点；密码页切分类直接列出账号。',tradeoff:'三组覆盖全部14个应用；网络表单仍共用系统输入器。'},
  {id:'folio',number:'12',name:'票签 · 纸质目录',subtitle:'黑白纸面、编号票签与全宽目录',try:'上下滚动整张清单，点击票签进入；设置页用右侧状态框标识开关。',tradeoff:'不使用纸张纹理图或阴影，靠实线和留白呈现层次；并非电子墨水屏。'},
  {id:'matrix',number:'13',name:'方阵 · 六键分页',subtitle:'固定六个大按键，蓝绿色掌机面板',try:'每页两列三行，底部箭头翻组；点击按键直接执行，不需要二次确认。',tradeoff:'只绘制当前一组；最后一组保留空位，避免按键位置变化。'},
  {id:'ribbon',number:'14',name:'层叠 · 拇指卡组',subtitle:'上一项、当前项、下一项同时可见',try:'点上、下卡片或左右滑动换项；点中央大卡片打开，底部有固定确认区。',tradeoff:'卡片只用不同宽度的色块，不使用立体透视或连续动画。'},
  {id:'split',number:'15',name:'分镜 · 双窗索引',subtitle:'左边找项目，右边看内容；琥珀色分屏',try:'左边列表可独立滚动并选择，右边显示大图标和操作；点击打开才进入。',tradeoff:'左列保留四个中文的宽度，操作区始终可见；详情页仍全屏展开。'}
];
const makeState = concept => ({concept,scene:'home',index:concept.id==='focus'?4:0,wifi:false,bt:false,motion:false,silent:false,brightness:60,filter:false,group:0,tab:0,indexScroll:0,panel:null,account:0,reveal:false,rec:'idle',seconds:0,files:1,toastTimer:null,gesture:null,suppressClick:0});
const instances=new Set();
const $screen=s=>s.element;
const button = (action, content, cls='', attrs='') => `<button type="button" data-action="${action}" class="${cls}" ${attrs}>${content}</button>`;
const appButton = (index, content, cls='') => button('app',content,cls,`data-app="${index}" aria-label="打开${APPS[index][0]}"`);
const pressed = value => `aria-pressed="${Boolean(value)}"`;
const head = (title, tail='DEMO') => `<div class="small-head"><strong>${title}</strong><small>${tail}</small></div>`;
const elapsed = s => `<span data-elapsed>${formatTime(s.seconds)}</span>`;
const formatTime = seconds => `${String(Math.floor(seconds/60)).padStart(2,'0')}:${String(seconds%60).padStart(2,'0')}`;
const recording = s => ['recording','paused'].includes(s.rec);
const playing = s => ['playing','paused-play'].includes(s.rec);
const recStatus = s => ({idle:'准备录音',recording:'正在录音',paused:'录音暂停',playing:'模拟播放','paused-play':'播放暂停'}[s.rec]);
const recordIcon = s => svg(s.rec==='recording'?'pause':s.rec==='paused'?'play':'record',s.rec==='idle'?'full-record':'');
const recordAttrs = s => `aria-label="${s.rec==='recording'?'暂停录音':s.rec==='paused'?'继续录音':'开始模拟录音'}" ${playing(s)?'disabled':''}`;
const stopAttrs = s => `aria-label="停止录音或播放" ${s.rec==='idle'?'disabled':''}`;
const playAttrs = s => `aria-label="播放或暂停演示录音" ${recording(s)?'disabled':''}`;

function dock(s) {
  return `<div class="dock">${[['home','home','桌面'],['apps','grid','应用'],['settings','settings','控制']].map(([scene,icon,name])=>button('scene',svg(icon)+name,s.scene===scene?'selected':'',`data-target="${scene}" ${pressed(s.scene===scene)}`)).join('')}</div>`;
}
function tileHome(s) {
  return `<div class="page tile-home"><div class="tile-matrix">${appButton(0,'<small>WED / 09.23</small><strong>18:20</strong><span>2026 · 雨墨</span>','tile tile-clock')}${appButton(4,svg('mic')+'<span>开始记录</span>','tile tile-record')}${appButton(2,`<span><strong>密码本</strong><small>14条演示</small></span>${svg('vault')}`,'tile tile-vault')}${appButton(6,svg('random')+'<span>随机</span>','tile tile-random')}<div class="tile-signal">${button('toggle',svg('wifi')+(s.wifi?'Wi-Fi ON':'Wi-Fi OFF'),'',`data-control="wifi" ${pressed(s.wifi)}`)}${button('toggle',svg('bluetooth')+(s.bt?'BT ON':'BT OFF'),'',`data-control="bt" ${pressed(s.bt)}`)}</div></div>${dock(s)}</div>`;
}
function tileApps(s) {
  return `<div class="page">${head('全部应用','14 APPS')}<div class="scroll"><div class="drawer-grid">${APPS.map(([name,icon],i)=>appButton(i,svg(icon)+`<span>${name}</span>`)).join('')}</div></div>${dock(s)}</div>`;
}
function tileSettings(s) {
  return `<div class="page">${head('控制中心')}<div class="control-tiles">${CONTROLS.slice(0,4).map(([id,icon,name])=>button('toggle',svg(icon)+`<span>${name}<small>${s[id]?'已开启':'已关闭'}</small></span>`,'control-tile '+(s[id]?'on':''),`data-control="${id}" ${pressed(s[id])}`)).join('')}</div><label class="brightness">${svg('sun')}<input data-brightness type="range" min="5" max="100" value="${s.brightness}" aria-label="演示屏幕亮度"><output>${s.brightness}%</output></label>${dock(s)}</div>`;
}
function tileVault(s) {
  return `<div class="page">${head('密码收藏','14 ITEMS')}<div class="category-tiles">${CATEGORIES.map(([name,icon,count],i)=>button('category',svg(icon)+`<small>${count}</small><span>${name}</span>`,'category-tile',`data-category="${i}"`)).join('')}</div>${dock(s)}</div>`;
}
function tileRecorder(s) {
  return `<div class="page">${head(recStatus(s),'REC / DEMO')}<div class="tile-rec-time"><strong>${elapsed(s)}</strong><small>双麦 · 演示</small></div><div class="tile-rec-controls">${button('record',recordIcon(s),'',recordAttrs(s))}${button('stop',svg('stop'),'',stopAttrs(s))}${button('play',svg(playing(s)?'pause':'play'),'',playAttrs(s))}</div><div class="file-strip">▸ 演示录音 · ${s.files} 个文件</div>${dock(s)}</div>`;
}

function sceneItems(s) {
  if(s.scene==='settings') return CONTROLS.map(([id,icon,name])=>({name,icon,tail:id==='brightness'?s.brightness+'%':id==='home'?'返回':s[id]?'ON':'OFF',id}));
  if(s.scene==='vault') return CATEGORIES.map(([name,icon,count])=>({name,icon,tail:count+' 条',id:name}));
  return APPS.map(([name,icon])=>({name,icon,tail:'打开应用',id:name}));
}
function orbit(s) {
  const items=sceneItems(s), index=Math.min(s.index,items.length-1), item=items[index];
  const page=Math.floor(index/6), visible=items.slice(page*6,page*6+6);
  const nodes=visible.map((node,i)=>{
    const angle=(-90+i*360/visible.length)*Math.PI/180, x=120+76*Math.cos(angle)-32, y=100+76*Math.sin(angle)-24;
    return button('select',svg(node.icon)+`<span>${node.name}</span>`,'orbit-node '+(index===page*6+i?'selected':''),`data-index="${page*6+i}" style="left:${x.toFixed(2)}px;top:${y.toFixed(2)}px" ${pressed(index===page*6+i)}`);
  }).join('');
  return `<div class="page"><div class="orbit-field" data-gesture><div class="orbit-track"></div>${nodes}${button('execute',svg(item.icon)+`<strong>${item.name}</strong><small>${s.scene==='home'?'OPEN':item.tail}</small>`,'orbit-center')}</div><div class="orbit-footer">${button('group-prev',svg('back'),'','aria-label="上一组或上一项"')}<span><b>${s.scene==='settings'?'控制轨道':s.scene==='vault'?'密码分类':'应用轨道'}</b>${page+1} / ${Math.ceil(items.length/6)}</span>${button('group-next',svg('next'),'','aria-label="下一组或下一项"')}</div></div>`;
}
function orbitRecorder(s) {
  return `<div class="page orbit-rec">${head('声音轨道','DEMO')}<div class="dial"><small>${recStatus(s)}</small><strong>${elapsed(s)}</strong>${button('record',recordIcon(s),'',recordAttrs(s))}</div><div class="transport">${button('home',svg('home'),'','aria-label="返回桌面"')}${button('stop',svg('stop'),'',stopAttrs(s))}${button('play',svg(playing(s)?'pause':'play'),'',playAttrs(s))}</div></div>`;
}

function focusFooter(action, title) {
  return `<div class="focus-footer">${button('previous',svg('back'),'','aria-label="上一张卡片"')}${button(action,title,'focus-action')}${button('next',svg('next'),'','aria-label="下一张卡片"')}</div>`;
}
function focus(s) {
  const items=sceneItems(s), i=s.index%items.length, item=items[i];
  const progress=`<div class="focus-progress" aria-hidden="true">${items.map((_,n)=>`<i class="${n===i?'current':''}"></i>`).join('')}</div>`;
  let content, actionTitle;
  if(s.scene==='home') {
    content=button('execute',`<span class="focus-index">${String(i+1).padStart(2,'0')}</span><span class="big-symbol">${svg(item.icon)}</span><h3>${item.name}</h3><p>${APPS[i][2]}</p>`,'focus-card');
    actionTitle='打开应用';
  } else if(s.scene==='settings') {
    content=`<div class="focus-control">${svg(item.icon)}<span class="focus-state">${item.tail}</span><h3>${item.name}</h3><p>${item.id==='brightness'?'每次点击增加 20%，循环调节':'点下方按钮执行当前操作'}</p></div>`;
    actionTitle=item.id==='brightness'?'调整亮度':item.id==='home'?'返回桌面':s[item.id]?'关闭':'开启';
  } else {
    content=button('execute',`<span class="focus-index">${String(i+1).padStart(2,'0')}</span><span class="big-symbol">${svg(item.icon)}</span><h3>${item.name}</h3><p>${item.tail}演示密码 · 点击展开账号</p>`,'focus-card');
    actionTitle='浏览分类';
  }
  return `<div class="page focus-page" data-gesture><div class="focus-header"><span>${s.scene==='home'?'YUMO / APPS':s.scene==='settings'?'CONTROL / DEMO':'VAULT / DEMO'}</span><b>${String(i+1).padStart(2,'0')} / ${String(items.length).padStart(2,'0')}</b></div>${content}${progress}${focusFooter('execute',actionTitle)}</div>`;
}
function focusRecorder(s) {
  return `<div class="page focus-page"><div class="focus-header"><span>YUMO / TAPE</span><b>DEMO ONLY</b></div><div class="cassette ${['recording','playing'].includes(s.rec)?'live':''}"><div class="tape-label"><b>A / 声音备忘</b><span>DUAL MIC</span></div><div class="reels"><i class="reel"></i><i class="reel"></i></div></div><div class="tape-time">${elapsed(s)}<small>${recStatus(s)}</small></div><div class="transport">${button('record',recordIcon(s),'primary',recordAttrs(s))}${button('stop',svg('stop'),'',stopAttrs(s))}${button('play',svg(playing(s)?'pause':'play'),'',playAttrs(s))}${button('home',svg('home'),'','aria-label="返回桌面"')}</div></div>`;
}

function railNav(s) {
  return `<div class="rail-nav">${[['home','grid','桌面'],['vault','vault','密码'],['recorder','mic','录音'],['settings','settings','设置']].map(([scene,icon,name])=>button('scene',svg(icon)+`<span>${name}</span>`,s.scene===scene?'selected':'',`data-target="${scene}" ${pressed(s.scene===scene)}`)).join('')}</div>`;
}
function railRow(action, icon, name, tail='', attrs='', cls='') {
  return button(action,svg(icon)+`<span>${name}</span><small>${tail}</small>`,'rail-row '+cls,attrs);
}
function rail(s) {
  let content;
  if(s.scene==='home') {
    content=`<div class="rail-top">${button('filter-all','全部',!s.filter?'selected':'',pressed(!s.filter))}${button('filter-favorite','常用',s.filter?'selected':'',pressed(s.filter))}</div><div class="scroll">${APPS.map(([name,icon],i)=>!s.filter||[0,2,4,7,11].includes(i)?railRow('app',icon,name,'›',`data-app="${i}"`):'').join('')}</div>`;
  } else if(s.scene==='settings') {
    content=`<div class="rail-title">设置<small>CONTROL</small></div><div class="scroll">${CONTROLS.map(([id,icon,name])=>railRow('toggle',icon,name,id==='brightness'?s.brightness+'%':id==='home'?'›':s[id]?'ON':'OFF',`data-control="${id}"`,s[id]===true?'on':'')).join('')}</div>`;
  } else if(s.scene==='vault') {
    content=`<div class="rail-title">密码本<small>14 ITEMS</small></div><div class="scroll">${CATEGORIES.map(([name,icon,count],i)=>railRow('fold',icon,name,`${count} ${s.group===i?'−':'+'}`,`data-category="${i}" aria-expanded="${s.group===i}"`)+(s.group===i?ACCOUNTS.slice(0,2).map(([account],n)=>button('account',`<span>${esc(account)}</span><small>›</small>`,'rail-row',`data-account="${n}"`)).join(''):'')).join('')}</div>`;
  } else {
    content=`<div class="rail-title">录音机<small>DEMO</small></div><div class="rail-rec-time">${elapsed(s)}</div><div class="rail-rec-status">${recStatus(s)} · 不访问麦克风</div><div class="rail-rec-controls">${button('record',recordIcon(s),'',recordAttrs(s))}${button('stop',svg('stop'),'',stopAttrs(s))}${button('play',svg(playing(s)?'pause':'play'),'',playAttrs(s))}</div><div class="rail-wave" aria-hidden="true">${[7,13,22,15,29,20,34,14,24,10,18,8].map(h=>`<i style="height:${h}px"></i>`).join('')}</div><div class="rail-fold">演示文件 / ${s.files} 个 · 00:38</div>`;
  }
  return `<div class="page rail-page">${railNav(s)}<div class="rail-content">${content}</div></div>`;
}

function terminal(s) {
  const items=sceneItems(s), selected=s.index%items.length, start=Math.floor(selected/5)*5;
  const title=s.scene==='home'?'YUMO / APP DIRECTORY':s.scene==='settings'?'SYS / CONTROL':'VAULT / CATEGORIES';
  return `<div class="page terminal-page"><div class="terminal-head"><span>${title}</span><span>${String(selected+1).padStart(2,'0')}/${items.length}</span></div><div class="terminal-list">${items.slice(start,start+5).map((item,n)=>button('select',`<span class="cursor">${selected===start+n?'▶':'·'}</span><span class="row-num">${String(start+n+1).padStart(2,'0')}</span><span class="row-name">${item.name}</span><small>${s.scene==='home'?'APP':item.tail}</small>`,'terminal-row '+(selected===start+n?'selected':''),`data-index="${start+n}" ${pressed(selected===start+n)}`)).join('')}</div><div class="terminal-keys">${button('previous',svg('next','up-icon')+'上一个')}${button('next',svg('next','down-icon')+'下一个')}${button('execute',svg('enter')+'确认','confirm')}</div></div>`;
}
function terminalRecorder(s) {
  return `<div class="page terminal-page"><div class="terminal-head"><span>RECORDER / DEMO</span><span>16K</span></div><div class="terminal-rec"><p>&gt; ${recStatus(s)}_</p><strong>${elapsed(s)}</strong><div class="scope" aria-hidden="true"><i style="height:8px"></i><i style="height:14px"></i><i style="height:28px"></i><i style="height:12px"></i><i style="height:20px"></i><i style="height:36px"></i><i style="height:12px"></i><i style="height:24px"></i><i style="height:16px"></i><i style="height:32px"></i><i style="height:18px"></i><i style="height:8px"></i></div><div class="file-info">/recordings/ DEMO_${s.files}.wav</div></div><div class="terminal-keys">${button('record',recordIcon(s),'confirm',recordAttrs(s))}${button('stop',svg('stop'),'',stopAttrs(s))}${button('play',svg(playing(s)?'pause':'play'),'',playAttrs(s))}${button('home',svg('home'),'','aria-label="返回桌面"')}</div></div>`;
}

// 11–15 share the same action, dialog and recording lifecycle as 06–10.
const APP_GROUPS=[[0,1,2,6],[3,10,11,12,13],[4,5,7,8,9]];
const sceneTitle=s=>({home:'应用目录',apps:'全部应用',settings:'系统设置',vault:'密码分类'}[s.scene]||'录音机');
const operation=s=>s.scene==='settings'?'执行操作':s.scene==='vault'?'浏览账号':'打开应用';
function itemButton(s,item,i,content,cls='') {
  if(s.scene==='settings')return button('toggle',content,cls,`data-control="${item.id}" ${pressed(s[item.id]===true)}`);
  if(s.scene==='vault')return button('category',content,cls,`data-category="${i}"`);
  return appButton(i,content,cls);
}
function tabs(s) {
  if(s.scene==='home'||s.scene==='apps') {
    const tab=s.tab%3;
    return `<div class="page tabs-page"><div class="tab-strip">${['日常','工具','媒体'].map((name,i)=>button('tab',name,i===tab?'selected':'',`data-tab="${i}" ${pressed(i===tab)}`)).join('')}</div><div class="tab-heading"><span>${['随手即用','连接与控制','声音与画面'][tab]}</span><small>${APP_GROUPS[tab].length} APPS</small></div><div class="scroll">${APP_GROUPS[tab].map(i=>appButton(i,svg(APPS[i][1])+`<span>${APPS[i][0]}</span>`+svg('next'),'tab-row')).join('')}</div></div>`;
  }
  if(s.scene==='vault')return `<div class="page tabs-page"><div class="tab-strip vault-tabs">${CATEGORIES.map(([name],i)=>button('tab',name,i===s.tab?'selected':'',`data-tab="${i}" ${pressed(i===s.tab)}`)).join('')}</div><div class="tab-heading"><span>${CATEGORIES[s.tab][0]}账号</span>${button('sync',svg('sync'),'','aria-label="模拟同步"')}</div><div class="scroll">${ACCOUNTS.map(([account,label],i)=>button('account',`<span>${esc(account)}</span><small>${label}</small>`,'account-row',`data-account="${i}"`)).join('')}</div></div>`;
  return `<div class="page tabs-page"><div class="tab-heading"><span>控制抽屉</span><small>MANUAL</small></div><div class="scroll tab-controls">${sceneItems(s).map((item,i)=>itemButton(s,item,i,svg(item.icon)+`<span>${item.name}</span><small>${item.tail}</small>`,'tab-control')).join('')}</div></div>`;
}
function folio(s) {
  return `<div class="page folio-page"><div class="folio-heading"><small>YUMO / FIELD NOTES</small><strong>${sceneTitle(s)}</strong></div><div class="scroll">${sceneItems(s).map((item,i)=>itemButton(s,item,i,`<small>${String(i+1).padStart(2,'0')}</small>${svg(item.icon)}<span>${item.name}</span><b>${s.scene==='home'?'›':item.tail}</b>`,'folio-row')).join('')}</div><div class="folio-footer">RAIN & INK <span>2026 / 09 / 23</span></div></div>`;
}
function matrix(s) {
  const items=sceneItems(s),page=Math.floor(s.index/6),count=Math.ceil(items.length/6);
  return `<div class="page matrix-page"><div class="matrix-grid">${Array.from({length:6},(_,n)=>{const i=page*6+n,item=items[i];return item?itemButton(s,item,i,svg(item.icon)+`<span>${item.name}</span>`+(s.scene==='settings'?`<small>${item.tail}</small>`:''),'matrix-cell'):'<div class="matrix-empty" aria-hidden="true"></div>';}).join('')}</div><div class="matrix-footer">${button('group-prev',svg('back'),'','aria-label="上一组"')}<span>${sceneTitle(s)} <small>${page+1}/${count}</small></span>${button('group-next',svg('next'),'','aria-label="下一组"')}</div></div>`;
}
function ribbon(s) {
  const items=sceneItems(s),i=s.index%items.length,prev=(i+items.length-1)%items.length,next=(i+1)%items.length;
  return `<div class="page ribbon-page" data-gesture><div class="ribbon-heading"><small>YUMO / STACK</small><span>${i+1} / ${items.length}</span></div><div class="ribbon-stack">${button('select',svg(items[prev].icon)+`<span>${items[prev].name}</span>`,'ribbon-card before',`data-index="${prev}"`)}${button('execute',svg(items[i].icon)+`<span>${items[i].name}</span><small>${items[i].tail}</small>`,'ribbon-card current')}${button('select',svg(items[next].icon)+`<span>${items[next].name}</span>`,'ribbon-card after',`data-index="${next}"`)}</div><div class="ribbon-footer">${button('previous',svg('back'),'','aria-label="上一项"')}${button('execute',operation(s),'stack-open')}${button('next',svg('next'),'','aria-label="下一项"')}</div></div>`;
}
function split(s) {
  const items=sceneItems(s),i=s.index%items.length,item=items[i];
  return `<div class="page split-page"><div class="split-heading"><strong>${sceneTitle(s)}</strong><small>${String(i+1).padStart(2,'0')} / ${items.length}</small></div><div class="split-body"><div class="split-index scroll">${items.map((row,n)=>button('select',row.name,n===i?'selected':'',`data-index="${n}" ${pressed(n===i)}`)).join('')}</div><div class="split-detail"><small>YUMO / ${String(i+1).padStart(2,'0')}</small>${svg(item.icon)}<strong>${item.name}</strong><span>${item.tail}</span>${button('execute',operation(s))}</div></div></div>`;
}
function extraRecorder(s) {
  const type=s.concept.id, meter=`<div class="extra-meter" aria-hidden="true">${[8,16,23,34,14,28,38,19,26,12].map(h=>`<i style="height:${h}px"></i>`).join('')}</div>`;
  const controls=`<div class="extra-transport">${button('record',recordIcon(s),'main-record',recordAttrs(s))}<div>${button('stop',svg('stop'),'',stopAttrs(s))}${button('play',svg(playing(s)?'pause':'play'),'',playAttrs(s))}${button('home',svg('home'),'','aria-label="返回桌面"')}</div></div>`;
  return `<div class="page extra-recorder ${type}-recorder"><div class="extra-rec-heading"><span>${recStatus(s)}</span><small>REC / DEMO</small></div><div class="extra-rec-readout"><strong>${elapsed(s)}</strong>${meter}</div>${controls}<div class="extra-rec-file"><span>演示录音</span><small>${s.files} FILE / 00:38</small></div></div>`;
}

function panelMarkup(s) {
  if(!s.panel) return '';
  let title, body, footer='';
  if(s.panel==='category') {
    title=CATEGORIES[s.group][0]+' · 演示账号';
    body=`<div class="scroll">${ACCOUNTS.map(([account,label],i)=>button('account',`<span>${esc(account)}</span><small>${label} · 仅演示</small>`,'account-row',`data-account="${i}"`)).join('')}</div>`;
    footer=`<div class="panel-footer">${button('sync',svg('sync')+'模拟同步')}</div>`;
  } else if(s.panel==='secret') {
    const [account,name,password]=ACCOUNTS[s.account];
    title=name;
    body=`<div class="account-meta">账号：<span>${esc(account)}</span></div><div class="secret-block"><small>仅演示用途 · ${s.reveal?'明文样式':'遮盖样式'}</small><div class="secret-value">${s.reveal?esc(password):'••••••••••••'}</div></div><div class="secret-row">${button('send-account','输入账号')}${button('send-password','输入密码')}</div>`;
    footer=`<div class="panel-footer">${button('reveal',svg('eye')+(s.reveal?'隐藏':'演示揭示'),'',pressed(s.reveal))}${button('enter',svg('enter')+'回车')}</div>`;
  } else if(s.panel==='flashlight') {
    title='手电筒 · 模拟'; body=button('flash','点击切换明暗','flash-demo');
  } else {
    title='随机 · 模拟'; body='<div class="clock-demo"><strong data-random>06</strong><p>1—99 · 仅示例交互</p></div>'; footer=`<div class="panel-footer">${button('roll','开始')}</div>`;
  }
  return `<div class="modal-layer"><section class="detail-panel" role="dialog" aria-modal="true" aria-label="${title}"><div class="panel-head">${button('panel-back',svg('back'),'','aria-label="返回上一层"')}<strong>${title}</strong>${button('close',svg('close'),'','aria-label="关闭详情"')}</div>${body}${footer}</section></div>`;
}
function render(s) {
  const screen=$screen(s), hadFocus=screen.contains(screen.getRootNode().activeElement); clearTimeout(s.toastTimer);
  let html;
  if(s.scene==='clock') html=`<div class="page"><yumo-clock theme="${s.concept.id}" face="digital"></yumo-clock></div>`;
  else switch(s.concept.id) {
    case 'tiles': html=({home:tileHome,apps:tileApps,settings:tileSettings,vault:tileVault,recorder:tileRecorder}[s.scene] || tileHome)(s); break;
    case 'orbit': html=s.scene==='recorder'?orbitRecorder(s):orbit(s); break;
    case 'focus': html=s.scene==='recorder'?focusRecorder(s):focus(s); break;
    case 'rail': html=rail(s); break;
    case 'terminal': html=s.scene==='recorder'?terminalRecorder(s):terminal(s); break;
    default: html=s.scene==='recorder'?extraRecorder(s):({tabs,folio,matrix,ribbon,split}[s.concept.id])(s); break;
  }
  screen.innerHTML=html+panelMarkup(s); screen.dataset.scene=s.scene;
  window.DevicePreview.adapt(screen); schedule(); s.onScene?.(s.scene);
  const index=screen.querySelector('.split-index');if(index)index.scrollTop=s.indexScroll;
  // Do not allow the obscured canvas to receive keyboard focus behind a dialog.
  if(s.panel) screen.querySelector('.page').inert=true;
  else if(hadFocus) screen.focus({preventScroll:true});
}
function navigate(s,scene) {
  if(s.onRoute?.(scene)===true)return;
  if(!['home','apps','settings','vault','recorder','clock'].includes(scene)) return;
  if(s.scene==='recorder' && scene!=='recorder') { s.rec='idle'; s.seconds=0; }
  s.scene=scene; s.index=0; s.group=0; s.tab=0; s.indexScroll=0; s.panel=null; s.reveal=false; s.gesture=null; s.suppressClick=0; render(s);
}
function message(s,text) {
  const screen=$screen(s); screen.querySelector('.toast')?.remove(); clearTimeout(s.toastTimer);
  const node=document.createElement('div'); node.className='toast'; node.textContent=text; node.setAttribute('role','status'); screen.append(node);
  s.toastTimer=setTimeout(()=>node.remove(),1900);
  window.DevicePreview.adapt(node);
  screen.dispatchEvent(new CustomEvent('preview-message',{bubbles:true,composed:true,detail:text}));
}
function openPanel(s,name) {
  s.panel=name; s.reveal=false; render(s); $screen(s).querySelector('.panel-head button').focus({preventScroll:true});
}
function closePanel(s) { s.panel=null; s.reveal=false; render(s); $screen(s).focus({preventScroll:true}); }
function openApp(s,index) {
  const target={0:'clock',1:'flashlight',2:'vault',4:'recorder',6:'random',11:'settings'}[index];
  if(target==='random') navigate(s,target);
  else if(target==='flashlight') openPanel(s,target);
  else if(target) navigate(s,target);
  else message(s,APPS[index][0]+'：本轮仅演示入口，不改变现有功能');
}
function toggleControl(s,id) {
  if(id==='home') { navigate(s,'home'); return; }
  if(id==='brightness') s.brightness=s.brightness>=100?20:s.brightness+20;
  else if(['wifi','bt','motion','silent'].includes(id)) s[id]=!s[id];
  else return;
  render(s);
}
function advance(s,delta) { s.index=(s.index+delta+sceneItems(s).length)%sceneItems(s).length; render(s); }
function execute(s) {
  if(s.scene==='settings') toggleControl(s,CONTROLS[s.index][0]);
  else if(s.scene==='vault') { s.group=s.index; openPanel(s,'category'); }
  else openApp(s,s.index);
}
function action(s,node) {
  const actionName=node.dataset.action;
  switch(actionName) {
    case 'scene': navigate(s,node.dataset.target); break;
    case 'home': navigate(s,'home'); break;
    case 'app': openApp(s,Number(node.dataset.app)); break;
    case 'toggle': toggleControl(s,node.dataset.control); break;
    case 'select': s.indexScroll=$screen(s).querySelector('.split-index')?.scrollTop||0; s.index=Number(node.dataset.index); render(s); break;
    case 'tab': s.tab=Number(node.dataset.tab); if(s.scene==='vault')s.group=s.tab; render(s); break;
    case 'previous': advance(s,-1); break;
    case 'next': advance(s,1); break;
    case 'group-prev': case 'group-next': {
      const length=sceneItems(s).length, delta=actionName==='group-next'?1:-1;
      if(length<=6) { if(s.concept.id!=='matrix')advance(s,delta); }
      else { s.index=((Math.floor(s.index/6)+delta+Math.ceil(length/6))%Math.ceil(length/6))*6; render(s); }
      break;
    }
    case 'execute': execute(s); break;
    case 'filter-all': case 'filter-favorite': s.filter=actionName==='filter-favorite'; render(s); break;
    case 'category': s.group=Number(node.dataset.category); openPanel(s,'category'); break;
    case 'fold': s.group=s.group===Number(node.dataset.category)?-1:Number(node.dataset.category); render(s); break;
    case 'account': s.account=Number(node.dataset.account); openPanel(s,'secret'); break;
    case 'close': closePanel(s); break;
    case 'panel-back': if(s.panel==='secret' && s.concept.id!=='rail') openPanel(s,'category'); else closePanel(s); break;
    case 'reveal': s.reveal=!s.reveal; render(s); $screen(s).querySelector('[data-action=reveal]').focus({preventScroll:true}); break;
    case 'send-account': message(s,'模拟输入账号 · 未发送蓝牙'); break;
    case 'send-password': message(s,'模拟输入密码 · 未发送蓝牙'); break;
    case 'enter': message(s,'模拟回车 · 未发送按键'); break;
    case 'sync': message(s,'模拟同步完成 · 未连接密码服务'); break;
    case 'flash': node.style.opacity=node.style.opacity==='0.2'?'1':'0.2'; break;
    case 'roll': $screen(s).querySelector('[data-random]').textContent=String(1+Math.floor(Math.random()*99)).padStart(2,'0'); break;
    case 'record':
      if(playing(s)) break;
      if(s.rec==='idle') { s.rec='recording'; s.seconds=0; }
      else s.rec=s.rec==='recording'?'paused':'recording';
      render(s); break;
    case 'stop':
      if(recording(s)) s.files++;
      s.rec='idle'; s.seconds=0; render(s); message(s,'模拟已停止 · 未写入 SD 卡'); break;
    case 'play':
      if(recording(s)) break;
      if(s.rec==='idle') { s.rec='playing'; s.seconds=0; }
      else s.rec=s.rec==='playing'?'paused-play':'playing';
      render(s); break;
  }
}

function mount(screen,concept,options={}) {
  const s=Object.assign(makeState(concept),options.state||{},{element:screen,onScene:options.onScene,onRoute:options.onRoute,visible:true});
  instances.add(s);

  screen.addEventListener('click',event=>{
    if(performance.now()<s.suppressClick) { event.preventDefault(); return; }
    const node=event.target.closest('button[data-action]');
    if(node && !node.disabled) action(s,node);
    else if(event.target.classList.contains('modal-layer')) closePanel(s);
  });
  screen.addEventListener('input',event=>{
    if(event.target.matches('[data-brightness]')) {
      s.brightness=Number(event.target.value); event.target.nextElementSibling.textContent=s.brightness+'%';
    }
  });
  screen.addEventListener('pointerdown',event=>{
    if(!s.panel && event.isPrimary && event.button===0 && event.target.closest('[data-gesture]') && !event.target.closest('input,select')) s.gesture={x:event.clientX,y:event.clientY,pointer:event.pointerId};
  });
  screen.addEventListener('pointerup',event=>{
    const start=s.gesture; s.gesture=null;
    if(!start || start.pointer!==event.pointerId) return;
    const scale=screen.getBoundingClientRect().width/240;
    const dx=(event.clientX-start.x)/scale, dy=(event.clientY-start.y)/scale;
    if(Math.abs(dx)>=28 && Math.abs(dx)>Math.abs(dy)*1.3) { s.suppressClick=performance.now()+350; advance(s,dx<0?1:-1); }
  });
  screen.addEventListener('pointercancel',()=>{s.gesture=null;});
  screen.addEventListener('pointerleave',event=>{if(event.buttons===0) s.gesture=null;});
  screen.addEventListener('keydown',event=>{
    if(event.key==='Escape') { if(s.panel) closePanel(s); else navigate(s,'home'); event.preventDefault(); return; }
    if(s.panel && event.key==='Tab') {
      const buttons=[...screen.querySelectorAll('.detail-panel button:not(:disabled)')];
      if(event.shiftKey && event.target===buttons[0]) { buttons.at(-1).focus(); event.preventDefault(); }
      else if(!event.shiftKey && event.target===buttons.at(-1)) { buttons[0].focus(); event.preventDefault(); }
      return;
    }
    if(s.panel || event.target.closest('input,select,yumo-clock') || ['recorder','clock'].includes(s.scene) || !['terminal','focus','orbit','ribbon','split'].includes(s.concept.id)) return;
    if(['ArrowRight','ArrowDown','ArrowLeft','ArrowUp'].includes(event.key)) {
      event.preventDefault(); advance(s,['ArrowLeft','ArrowUp'].includes(event.key)?-1:1); screen.focus({preventScroll:true});
    } else if(event.key==='Enter' && event.target===screen) { event.preventDefault(); execute(s); }
  });
  render(s);
  return {state:s,navigate:scene=>navigate(s,scene),render:()=>render(s),
    setVisible:value=>{s.visible=value;schedule();},
    dispose(){instances.delete(s);clearTimeout(s.toastTimer);s.rec='idle';s.panel=null;s.reveal=false;screen.replaceChildren();schedule();}};
}

let timer=null;
function needsTicks(s){return s.visible&&s.element.isConnected&&s.scene==='recorder'&&['recording','playing'].includes(s.rec);}
function schedule(){
  const needed=!document.hidden&&[...instances].some(needsTicks);
  if(!needed&&timer!==null){clearInterval(timer);timer=null;}
  if(needed&&timer===null)timer=setInterval(()=>{
    for(const s of instances)if(needsTicks(s)){
      s.seconds++;
      if(s.rec==='playing'&&s.seconds>=38){s.rec='idle';s.seconds=0;render(s);}
      else $screen(s).querySelectorAll('[data-elapsed]').forEach(n=>n.textContent=formatTime(s.seconds));
      const p=[[14,0],[27,14],[14,27],[0,14]][s.seconds%4];
      $screen(s).querySelectorAll('.reel').forEach(n=>{n.style.setProperty('--reel-x',p[0]+'px');n.style.setProperty('--reel-y',p[1]+'px');});
    }
  },1000);
}
document.addEventListener('visibilitychange',schedule);
window.addEventListener('pagehide',()=>{clearInterval(timer);timer=null;instances.forEach(s=>clearTimeout(s.toastTimer));});
window.addEventListener('pageshow',schedule);
window.LayoutPreview={themes:CONCEPTS,mount,get stats(){return {mounted:instances.size,timerRunning:timer!==null};}};
})();
