/* Reusable list and network renderer, synthetic UI only. No fetch, device APIs, clipboard or vault access. */
'use strict';
(() => {

const ICONS = new Set(["clock","flashlight","vault","keyboard","mic","music","random","game","text","photo","lan","settings","gravity","touch","wifi","bluetooth","battery","sun","power","info","arrow","back","home","check","sync","eye","enter","globe","work","grid","folder","user","shield","delete","record","pause","play","stop","rewind","forward","trash","search"]);
const icon=(name,cls='')=>window.DevicePreview.icon('legacy',ICONS.has(name)?name:'grid',cls);
const escapeHTML = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

const THEMES = [
  {id:'clear',number:'01',name:'澄蓝 · 轻快卡片',family:'CLEAR / EVERYDAY',description:'干净浅底与明确的蓝色操作。最接近现有交互，圆角克制，层级清楚。',tags:['轻量平面','清晰蓝色','7 px 圆角'],colors:['#eef2f7','#2868e8','#24334a'],recommended:true},
  {id:'sage',number:'02',name:'青苔 · 柔和色阶',family:'TONAL / ORGANIC',description:'低饱和绿、柔和色面与更饱满的圆角。收敛装饰，让小屏看起来更亲和。',tags:['同色系层次','柔和圆角','圆形图标底'],colors:['#edf1e6','#c3d9b7','#476c50']},
  {id:'night',number:'03',name:'夜航 · 深色仪表',family:'DARK / FOCUSED',description:'深底浅字，亮蓝突出可操作区域。少用分割线，把注意力留给内容。',tags:['深色模式','高辨识强调','暗色卡片'],colors:['#101419','#284262','#83bcff']},
  {id:'warm',number:'04',name:'砂岩 · 温暖留白',family:'WARM / TACTILE',description:'奶油底、陶土橙与细腻描边。保留紧凑密度，为工具界面增加一点温度。',tags:['暖色纸感','细边框','柔和橙色'],colors:['#f6f0e7','#f7dbca','#b95435']},
  {id:'mono',number:'05',name:'墨线 · 精密工具',family:'MONO / UTILITY',description:'接近直角的组件、单色线框与明确分区。规整、克制，更像一件随手工具。',tags:['单色线框','2 px 圆角','边界明确'],colors:['#f1f2ee','#d4e0ce','#344f3d']}
];
const APPS = [
  ['时钟','clock','clock'],['手电筒','flashlight'],['密码本','vault','vault'],['蓝牙键鼠','keyboard'],
  ['录音机','mic','recorder'],['蓝牙音乐','music'],['随机','random','random'],['FC游戏','game'],
  ['文本','text'],['相册','photo'],['局域网','lan'],['设置','settings','settings'],['重力球','gravity'],['触控展示','touch']
];
const CATEGORY = {work:['工作','work',3],app:['应用','grid',4],web:['网页','globe',5],other:['其他','folder',0]};
const initialState = theme => ({theme,scene:'home',wifi:false,bt:false,motion:false,ssid:'Yumo-Demo',formSsid:'',formPassword:'',manual:false,focusField:'formPassword',keyboard:false,charSet:'abc',char:'a',superMode:false,revealed:false,pin:'',pinReturn:'vault',pinPurpose:'',category:'web',account:'demo@example.com',recMode:'idle',seconds:0,files:1,modal:false,toastTimer:null});
const instances=new Set();
const $screen=s=>s.element;
function btn(action, content, cls='', label='', extra='') {
  return `<button type="button" data-action="${action}" class="${cls}"${label?` aria-label="${escapeHTML(label)}"`:''} ${extra}>${content}</button>`;
}
function nav(title, back='home', right='') {
  return `<div class="nav">${btn('back',icon('back'),'nav-back','返回',`data-back="${back}"`)}<span class="nav-title">${title}</span>${right}</div>`;
}
function row(action, symbol, title, sub='', tail='', extra='', cls='') {
  return btn(action, `${symbol?`<span class="row-icon">${icon(symbol)}</span>`:''}<span class="row-copy"><span class="row-title">${escapeHTML(title)}</span>${sub?`<small class="row-sub">${escapeHTML(sub)}</small>`:''}</span>${tail}`, 'list-row '+cls,'',extra);
}
const chevron = () => `<span class="row-tail">${icon('arrow')}</span>`;
const toggle = on => `<span class="toggle${on?' on':''}" aria-hidden="true"></span>`;

function home(s) {
  return `<div class="status"><span>18:20</span><span class="status-spacer"></span>${s.bt?icon('bluetooth'):''}${s.wifi?icon('wifi'):''}<span>82</span>${icon('battery','battery')}</div>
  <div class="scroll"><div class="app-grid">${APPS.map(([name,symbol,scene])=>btn('app',`<span class="app-icon">${icon(symbol)}</span><span class="app-name">${name}</span>`,'app-tile',name,`data-target="${scene||''}" data-name="${name}"`)).join('')}</div></div>`;
}
function settings(s) {
  const items = [
    ['wifi','wifi','Wi-Fi',s.wifi?'已连接':'已关闭'],['bluetooth','bluetooth','蓝牙',s.bt?'等待连接':'已关闭'],
    ['display','sun','显示与声音','60%'],['motion','gravity','运动感应',s.motion?'已开启':'已关闭'],
    ['system','power','系统与电源',''],['about','info','关于设备','雨墨']
  ];
  return `<div class="scroll list">${items.map(([target,symbol,title,tail])=>btn('setting',`<span class="row-icon">${icon(symbol)}</span><span class="row-copy"><span class="row-title">${title}</span></span><span class="row-tail">${tail}${icon('arrow')}</span>`,'setting-row','',`data-target="${target}"`)).join('')}</div>`;
}
function wifi(s) {
  const networks = [['Yumo-Demo','已连接 · 信号良好'],['Studio-Guest','需要密码 · 信号良好'],['Home-Network','已保存 · 信号一般']];
  if(s.ssid && !networks.some(([name])=>name===s.ssid)) networks.unshift([s.ssid,'已连接 · 信号良好']);
  return nav('Wi-Fi','settings')+btn('toggle-wifi',`<span>Wi-Fi</span>${toggle(s.wifi)}`,'setting-row wifi-toggle','Wi-Fi开关',`role="switch" aria-checked="${s.wifi}"`)+
    (s.wifi?`<div class="section-label"><span>可用网络 · 演示</span><span>${networks.length} 个</span></div><div class="scroll list">${networks.map(([name,sub])=>row('network','wifi',name,s.ssid===name?'已连接 · 信号良好':(sub.startsWith('已连接')?'需要密码 · 信号良好':sub),s.ssid===name?`<span class="row-tail accent">${icon('check')}</span>`:chevron(),`data-ssid="${escapeHTML(name)}"`)).join('')}</div>`:`<div class="scroll empty-state">${icon('wifi')}<span>Wi-Fi 已关闭</span><span>点击上方开关查看演示网络</span></div>`)+
    `<div class="split-footer">${btn('scan',icon('search')+'搜索','','搜索网络',s.wifi?'':'disabled')}${btn('manual','手动添加','','手动添加网络',s.wifi?'':'disabled')}</div>`;
}
function charsetOptions(s) {
  const chars = {abc:'abcdefghijklmnopqrstuvwxyz',ABC:'ABCDEFGHIJKLMNOPQRSTUVWXYZ',num:'0123456789',symbol:'!@#$%^&*()-_=+[]{};:,.?/ '};
  return [...chars[s.charSet]].map(c=>`<option value="${escapeHTML(c)}"${c===s.char?' selected':''}>${c===' '?'空格':escapeHTML(c)}</option>`).join('');
}
function wifiForm(s) {
  return nav(s.manual?'手动添加':'连接网络','wifi',btn('connect','连接','nav-action'))+
    `<div class="field-pane">${s.manual?`<input class="network-field" type="text" data-field="formSsid" aria-label="网络名称" placeholder="网络名称" value="${escapeHTML(s.formSsid)}" maxlength="32">`:`<div class="network-label">${icon('wifi')}<span>${escapeHTML(s.formSsid)}</span></div>`}
    <input class="network-field" type="text" data-field="formPassword" aria-label="Wi-Fi密码（演示）" placeholder="点击输入密码" value="${escapeHTML(s.formPassword)}" maxlength="63" autocomplete="off" spellcheck="false"><p class="network-help">模拟配置，不会连接真实 Wi-Fi</p></div>
    <div class="soft-keyboard" ${s.keyboard?'':'hidden'}><select data-keyset hidden aria-label="字符类型"><option value="abc"${s.charSet==='abc'?' selected':''}>小写 abc</option><option value="ABC"${s.charSet==='ABC'?' selected':''}>大写 ABC</option><option value="num"${s.charSet==='num'?' selected':''}>数字 123</option><option value="symbol"${s.charSet==='symbol'?' selected':''}>符号 #+=</option></select><select data-keychar hidden aria-label="选择字符">${charsetOptions(s)}</select>${btn('pick-set',({abc:'小写 abc',ABC:'大写 ABC',num:'数字 123',symbol:'符号 #+='})[s.charSet]+' ▾','','选择字符类型')}${btn('pick-char',(s.char===' '?'空格':escapeHTML(s.char))+' ▾','','选择字符')}${btn('key-delete',icon('delete'),'','删除一个字符')}${btn('key-insert','输入','insert','输入所选字符')}</div>`;
}
function bluetooth(s) {
  return nav('蓝牙','settings')+btn('toggle-bt',`<span>蓝牙</span>${toggle(s.bt)}`,'setting-row wifi-toggle','蓝牙开关',`role="switch" aria-checked="${s.bt}"`)+`<div class="scroll">${s.bt?`<div class="section-label">已配对 · 演示数据</div>${row('peer','keyboard','Demo Computer','00:11:22:33:44:55',chevron())}<p class="network-help">点击允许发现后仅模拟等待连接，不主动连接其他设备。</p>`:'<div class="empty-state">'+icon('bluetooth')+'<span>蓝牙已关闭</span></div>'}</div><div class="split-footer">${btn('discover','允许发现','','允许发现设备',s.bt?'':'disabled')}</div>`;
}
function vault(s) {
  return nav('密码本','home',btn('change-pin','改密','nav-action','修改密码（演示）'))+`<div class="scroll list">${Object.entries(CATEGORY).map(([id,[name,sym,count]])=>row('category',sym,name,'',`<span class="category-count">${count}</span>${chevron()}`,`data-category="${id}"`,'vault-row')).join('')}</div><div class="split-footer">${btn('sync',icon('sync')+'同步')}${btn('vault-connection',icon('bluetooth')+'连接')}${btn('super',icon('shield')+'超管',s.superMode?'active':'','超管模式',`aria-pressed="${s.superMode}"`)}</div>`;
}
function accounts(s) {
  const accounts = s.category==='web' ? [['demo@example.com',3],['studio@example.com',2]] : [['办公账号',2],['设备账号',1]];
  return nav(CATEGORY[s.category][0]+'账号','vault')+`<div class="scroll list">${accounts.map(([name,count])=>row('account','user',name,'',`<span class="category-count">${count}</span>${chevron()}`,`data-account="${name}"`)).join('')}</div>`;
}
function secrets(s) {
  const show = s.revealed || s.superMode;
  const samples = [['日常登录 · example.com','Demo-Only!16'],['开发工具 · tools.example','Yumo-Sample#8'],['社区账户 · community.example','Preview-NotReal!']];
  return nav('用途与密码','accounts',btn('reveal',icon('eye'),'nav-icon '+(show?'active':''),'显示或隐藏本页密码',`aria-pressed="${show}" ${s.superMode?'disabled':''}`)+btn('enter','回车','nav-action'))+`<div class="scroll"><div class="vault-name">名称：演示网页账号</div><div class="account-line"><span>账号：${escapeHTML(s.account)}</span>${btn('send-account','输入','send','模拟输入账号')}</div>${samples.map(([purpose,password])=>`<div class="password-purpose">${purpose}</div><div class="password-line"><span>${show?escapeHTML(password):'••••••••••••'}</span>${btn('send-password','输入','send','模拟输入密码')}</div>`).join('')}</div>`;
}
function pin(s) {
  return `<div class="pin-prompt" role="status">${s.pin?'● '.repeat(s.pin.length):'管理员密码 · 演示1234'}</div><div class="pin-grid">${['1','2','3','4','5','6','7','8','9'].map(n=>btn('pin-digit',n,'',n,`data-digit="${n}"`)).join('')}${btn('pin-delete',icon('delete'),'','删除密码数字')}${btn('pin-digit','0','','0','data-digit="0"')}${btn('pin-confirm',icon('check'),'pin-confirm','确认管理员密码')}</div>`;
}
const formattedTime = seconds => `${String(Math.floor(seconds/3600)).padStart(2,'0')}:${String(Math.floor(seconds/60)%60).padStart(2,'0')}:${String(seconds%60).padStart(2,'0')}`;
function recorder(s) {
  const playing = s.recMode==='playing' || s.recMode==='paused-play';
  const active = s.recMode==='recording';
  const paused = s.recMode==='paused';
  const titles = {idle:'准备录音',recording:'正在录音',paused:'录音已暂停',playing:'正在播放','paused-play':'播放已暂停'};
  return `<div class="record-title"><span>${icon('mic')}<span data-record-title>${titles[s.recMode]}</span></span><small class="record-tag">DEMO / 16K</small></div><div class="record-time-row"><span class="record-time" data-record-time>${formattedTime(s.seconds)}</span><div class="vu${active||s.recMode==='playing'?' live':''}" aria-label="模拟音量指示">${[5,8,12,17,20,14,18,11].map(h=>`<i style="height:${h}px"></i>`).join('')}</div></div>
  <div class="record-controls">${btn('record',icon(active?'pause':paused?'play':'record'),'record-main '+(active?'recording':paused?'paused':''),active?'暂停录音':paused?'继续录音':'开始模拟录音',playing?'disabled':'')}<div class="record-side">${btn('stop-record',icon('stop'),'record-stop','停止并保存模拟录音',playing||s.recMode==='idle'?'disabled':'')}<div class="record-transport">${btn('rewind',icon('rewind'),'','模拟2倍快退',playing?'':'disabled')}${btn('play-toggle',icon(s.recMode==='paused-play'?'play':'pause'),'','暂停或继续播放',playing?'':'disabled')}${btn('forward',icon('forward'),'','模拟2倍快进',playing?'':'disabled')}</div></div></div>
  <div class="record-list-label">录音列表 · ${s.files} 个演示文件</div><div class="scroll">${s.files?`<div class="record-file">${btn('play-file','<strong>2026-09-23_18-20-06.wav</strong><small>00:38 · 双麦录音</small>','record-file-open','播放演示录音',s.recMode==='idle'?'':'disabled')}${btn('delete-file',icon('trash'),'record-file-delete','删除演示录音',s.recMode==='idle'?'':'disabled')}</div>`:'<p class="network-help">暂无演示文件，可试一下录音按钮</p>'}</div>`;
}
function clock(s) {
  return `<yumo-clock theme="${s.theme.id}" face="digital"></yumo-clock>`;
}
const renderers = {home,settings,wifi,wifiForm,bluetooth,vault,accounts,secrets,pin,recorder,clock};
function render(s) {
  const screen = $screen(s);
  clearTimeout(s.toastTimer);
  screen.innerHTML = `<div class="page ${s.scene==='clock'?'clock-page':''}">${renderers[s.scene](s)}</div>`;
  screen.dataset.scene = s.scene;
  if (s.modal) showDeleteModal(s);
  window.DevicePreview.adapt(screen); schedule(); s.onScene?.(s.scene);
}
function navigate(s, target) {
  if (!(target in renderers)) return;
  if(s.onRoute?.(target)===true)return;
  if (s.scene==='secrets' && target!=='pin') s.revealed = false;
  if (s.scene==='recorder' && target!=='recorder') { s.recMode='idle'; s.seconds=0; }
  if (target==='home') { s.superMode=false; s.revealed=false; }
  s.scene=target; s.keyboard=false; s.modal=false; s.pin=''; render(s);
}
function toast(s, message) {
  const screen = $screen(s);
  screen.querySelector('.toast')?.remove(); clearTimeout(s.toastTimer);
  const element = document.createElement('div'); element.className='toast'; element.textContent=message; element.setAttribute('role','status'); screen.append(element); window.DevicePreview.adapt(element);
  s.toastTimer = setTimeout(()=>element.remove(),2000);
  screen.dispatchEvent(new CustomEvent('preview-message',{bubbles:true,composed:true,detail:message}));
}
function showDeleteModal(s) {
  const screen = $screen(s);
  screen.insertAdjacentHTML('beforeend',`<div class="modal-shade"><section class="modal" role="dialog" aria-modal="true" aria-label="删除录音确认"><h3>删除这段录音？</h3><p>这是模拟文件，仅在当前预览中移除。设备 SD 卡不会改变。</p><div class="modal-actions">${btn('cancel-delete','取消')}${btn('confirm-delete','确认删除','confirm')}</div></section></div>`);
  screen.querySelector('.page').inert=true;
  window.DevicePreview.adapt(screen);
  screen.querySelector('[data-action="cancel-delete"]').focus({preventScroll:true});
}
function requestPin(s, purpose) { s.pinReturn=s.scene; s.pinPurpose=purpose; s.pin=''; navigate(s,'pin'); }
function updateKeyboard(s, focus) {
  s.keyboard=true; s.focusField=focus;
  const screen=$screen(s);
  screen.querySelector('.soft-keyboard').hidden=false;
  screen.querySelector(`[data-field="${focus}"]`)?.scrollIntoView({block:'nearest'});
}
function closePicker(s){
  const screen=$screen(s),picker=screen.querySelector('.key-picker');if(!picker)return;
  const kind=picker.dataset.kind;picker.remove();screen.querySelector('.page').inert=false;
  screen.querySelector(`[data-action="pick-${kind}"]`)?.focus({preventScroll:true});
}
function openPicker(s,kind){
  const screen=$screen(s),select=screen.querySelector(kind==='set'?'[data-keyset]':'[data-keychar]');
  screen.querySelector('.page').inert=true;
  screen.insertAdjacentHTML('beforeend',`<section class="key-picker" data-kind="${kind}" role="dialog" aria-modal="true" aria-label="选择键盘字符"><div class="picker-head">${btn('close-picker',icon('back'),'','返回键盘')}<span>${kind==='set'?'字符类型':'选择字符'}</span></div><div class="picker-list">${[...select.options].map(o=>btn('pick-value',escapeHTML(o.textContent),'',o.textContent,`data-value="${escapeHTML(o.value)}" aria-pressed="${o.selected}"`)).join('')}</div></section>`);
  window.DevicePreview.adapt(screen);screen.querySelector('.key-picker [aria-pressed=true]')?.scrollIntoView({block:'nearest'});
  screen.querySelector('.key-picker button').focus({preventScroll:true});
}

function action(s, button) {
  const name=button.dataset.action;
  switch(name) {
    case 'pick-set':openPicker(s,'set');break;
    case 'pick-char':openPicker(s,'char');break;
    case 'close-picker':closePicker(s);break;
    case 'pick-value':{
      const screen=$screen(s),kind=screen.querySelector('.key-picker').dataset.kind,select=screen.querySelector(kind==='set'?'[data-keyset]':'[data-keychar]');
      select.value=button.dataset.value;select.dispatchEvent(new Event('change',{bubbles:true}));
      screen.querySelector('[data-action=pick-set]').textContent=screen.querySelector('[data-keyset]').selectedOptions[0].textContent+' ▾';
      screen.querySelector('[data-action=pick-char]').textContent=(s.char===' '?'空格':s.char)+' ▾';
      closePicker(s);window.DevicePreview.adapt(screen);break;
    }
    case 'app': if(button.dataset.target) navigate(s,button.dataset.target); else toast(s,'本轮仅提供上方六类页面的风格样例'); break;
    case 'back': navigate(s,button.dataset.back); break;
    case 'setting': {
      const target=button.dataset.target;
      if(target==='wifi'||target==='bluetooth') navigate(s,target);
      else if(target==='motion') { s.motion=!s.motion; render(s); toast(s,'仅模拟运动开关，未访问传感器'); }
      else toast(s,target==='about'?'雨墨 · 240 × 240 · 独立样式预览':target==='system'?'仅预览样式，不执行关机或 SD 操作':'保留现有显示与声音功能，待选定样式适配');
      break;
    }
    case 'toggle-wifi': s.wifi=!s.wifi; render(s); break;
    case 'network': s.formSsid=button.dataset.ssid; s.formPassword=''; s.manual=false; navigate(s,'wifiForm'); break;
    case 'manual': s.formSsid=''; s.formPassword=''; s.manual=true; navigate(s,'wifiForm'); break;
    case 'scan': toast(s,'模拟搜索完成 · 已刷新演示网络列表'); break;
    case 'connect':
      if(!s.formSsid.trim()) toast(s,'请填写演示网络名称');
      else if(s.formPassword.length<8) toast(s,'演示密码至少 8 个字符');
      else { s.ssid=s.formSsid; navigate(s,'wifi'); toast(s,'模拟连接成功，不操作设备 Wi-Fi'); } break;
    case 'key-delete': case 'key-insert': {
      const input=$screen(s).querySelector(`[data-field="${s.focusField}"]`);
      const current=s[s.focusField];
      if(name==='key-delete') s[s.focusField]=current.slice(0,-1);
      else if(current.length < (s.focusField==='formSsid'?32:63)) s[s.focusField]=current+s.char;
      if(input) input.value=s[s.focusField]; break;
    }
    case 'toggle-bt': s.bt=!s.bt; render(s); break;
    case 'discover': toast(s,'模拟允许发现 · 仅等待连接，不主动回连'); break;
    case 'peer': toast(s,'仅模拟手动邀请，不发起真实蓝牙连接'); break;
    case 'category':
      s.category=button.dataset.category;
      if(CATEGORY[s.category][2]) navigate(s,'accounts'); else toast(s,'此演示分类暂时为空'); break;
    case 'account': s.account=button.dataset.account; s.revealed=false; navigate(s,'secrets'); break;
    case 'sync': toast(s,'模拟同步完成 · 12 条演示数据（未联网）'); break;
    case 'vault-connection': navigate(s,'wifi'); break;
    case 'super': if(s.superMode) { s.superMode=false; render(s); } else requestPin(s,'super'); break;
    case 'reveal': if(s.revealed) { s.revealed=false; render(s); } else requestPin(s,'reveal'); break;
    case 'change-pin': toast(s,'修改密码交互沿用现状；预览不保存任何密码'); break;
    case 'pin-digit':
      if(s.pin.length<12) s.pin+=button.dataset.digit;
      $screen(s).querySelector(`.pin-prompt`).textContent='● '.repeat(s.pin.length); break;
    case 'pin-delete': s.pin=s.pin.slice(0,-1); $screen(s).querySelector(`.pin-prompt`).textContent=s.pin?'● '.repeat(s.pin.length):'管理员密码 · 演示1234'; break;
    case 'pin-confirm': {
      const success=s.pin==='1234',purpose=s.pinPurpose,destination=s.pinReturn;
      navigate(s,destination);
      if(success) { if(purpose==='super') s.superMode=true; else s.revealed=true; render(s); }
      else toast(s,'验证失败，已返回原页面（演示 PIN：1234）'); break;
    }
    case 'send-account': toast(s,'模拟输入账号 · 未发送蓝牙'); break;
    case 'send-password': toast(s,'模拟输入密码 · 未发送蓝牙'); break;
    case 'enter': toast(s,'模拟回车 · 未发送按键'); break;
    case 'record':
      if(s.recMode==='idle') { s.recMode='recording'; s.seconds=0; }
      else s.recMode=s.recMode==='recording'?'paused':'recording';
      render(s); break;
    case 'stop-record': s.recMode='idle'; s.files=1; render(s); toast(s,'模拟保存完成，不写入 SD 卡'); break;
    case 'play-file': s.recMode='playing'; s.seconds=0; render(s); break;
    case 'play-toggle': s.recMode=s.recMode==='playing'?'paused-play':'playing'; render(s); break;
    case 'forward': s.seconds=Math.min(37,s.seconds+2); render(s); break;
    case 'rewind': s.seconds=Math.max(0,s.seconds-2); render(s); break;
    case 'delete-file': s.modal=true; showDeleteModal(s); break;
    case 'cancel-delete': s.modal=false; render(s); break;
    case 'confirm-delete': s.files=0; s.modal=false; render(s); break;
  }
}
function mount(screen,theme,options={}) {
  const s=Object.assign(initialState(theme),options.state||{},{element:screen,onScene:options.onScene,onRoute:options.onRoute,visible:true});
  instances.add(s);

  screen.addEventListener('click', event=>{
    const button=event.target.closest('button[data-action]');
    if(button && !button.disabled) action(s,button);
    if(s.scene==='wifiForm' && !event.target.closest('.network-field,.soft-keyboard,.key-picker')) {
      s.keyboard=false; const keyboard=screen.querySelector('.soft-keyboard'); if(keyboard) keyboard.hidden=true;
      if(screen.getRootNode().activeElement?.matches('.network-field')) screen.getRootNode().activeElement.blur();
    }
  });
  screen.addEventListener('focusin',event=>{ if(event.target.matches('[data-field]')) updateKeyboard(s,event.target.dataset.field); });
  screen.addEventListener('input',event=>{ if(event.target.matches('[data-field]')) s[event.target.dataset.field]=event.target.value; });
  screen.addEventListener('change',event=>{
    if(event.target.matches('[data-keyset]')) { s.charSet=event.target.value; s.char={abc:'a',ABC:'A',num:'0',symbol:'!'}[s.charSet]; screen.querySelector('[data-keychar]').innerHTML=charsetOptions(s); }
    if(event.target.matches('[data-keychar]')) s.char=event.target.value;
  });
  screen.addEventListener('keydown',event=>{
    if(event.key==='Escape') {
      if(screen.querySelector('.key-picker')){closePicker(s);event.preventDefault();}
      else if(s.modal) { s.modal=false; render(s); }
      else if(s.scene==='pin') navigate(s,s.pinReturn);
      else if(s.scene==='wifiForm' && s.keyboard) { s.keyboard=false; screen.querySelector('.soft-keyboard').hidden=true; event.target.blur(); }
    }
    if(screen.querySelector('.key-picker')&&event.key==='Tab'){
      const buttons=[...screen.querySelectorAll('.key-picker button')];
      if(event.shiftKey&&event.target===buttons[0]){event.preventDefault();buttons.at(-1).focus();}
      else if(!event.shiftKey&&event.target===buttons.at(-1)){event.preventDefault();buttons[0].focus();}
    }
    if(s.modal && event.key==='Tab') {
      const buttons=[...screen.querySelectorAll('.modal button')];
      if(event.shiftKey && event.target===buttons[0]) { event.preventDefault(); buttons.at(-1).focus(); }
      else if(!event.shiftKey && event.target===buttons.at(-1)) { event.preventDefault(); buttons[0].focus(); }
    }
  });
  render(s);
  return {state:s,navigate:target=>navigate(s,target),render:()=>render(s),
    setVisible:value=>{s.visible=value;schedule();},
    dispose(){instances.delete(s);clearTimeout(s.toastTimer);s.recMode='idle';s.superMode=false;s.revealed=false;s.pin='';screen.replaceChildren();schedule();}};
}

let timer=null;
function needsTicks(s){return s.visible&&s.element.isConnected&&s.scene==='recorder'&&['recording','playing'].includes(s.recMode);}
function schedule(){
  const needed=!document.hidden&&[...instances].some(needsTicks);
  if(!needed&&timer!==null){clearInterval(timer);timer=null;}
  if(needed&&timer===null)timer=setInterval(()=>{
    for(const s of instances)if(needsTicks(s)){
      s.seconds++;
      if(s.recMode==='playing'&&s.seconds>=38){s.recMode='idle';s.seconds=0;render(s);}
      else{$screen(s).querySelector('[data-record-time]').textContent=formattedTime(s.seconds);
        $screen(s).querySelectorAll('.vu i').forEach((bar,i)=>bar.style.height=(5+(i*7+s.seconds*3)%16)+'px');}
    }
  },1000);
}
document.addEventListener('visibilitychange',schedule);
window.addEventListener('pagehide',()=>{clearInterval(timer);timer=null;instances.forEach(s=>clearTimeout(s.toastTimer));});
window.addEventListener('pageshow',schedule);
window.LegacyPreview={themes:THEMES,mount,get stats(){return {mounted:instances.size,timerRunning:timer!==null};}};
})();
