/* One selected theme, seven independent app previews and three clock faces. */
'use strict';
(() => {
  const C=window.ClockPreview,L=window.LegacyPreview,N=window.LayoutPreview;
  const themes=C.themes,cards=new Map();
  const sceneNames={home:'应用列表',settings:'设置',wifi:'Wi-Fi',bluetooth:'蓝牙',vault:'密码本',recorder:'录音机',random:'随机',clock:'时钟'};
  const scenes=Object.keys(sceneNames),appScenes=scenes.filter(key=>key!=='clock');
  const hints={home:'滚动、点选或滑动，体验本主题的应用入口。',settings:'查看控制布局，点击开关试试选中状态。',wifi:'打开模拟 Wi-Fi，点网络或手动输入，试用共享键盘。',bluetooth:'打开模拟蓝牙，查看设备列表与允许发现交互。',vault:'浏览演示分类、账号与密码；所有数据均为虚构。',recorder:'开始、暂停、继续与播放；不会访问麦克风或 SD 卡。',random:'八种模式与石头剪刀布 PK。屏幕聚焦后 B=BOOT 蓝方、K=KEY 红方；普通模式两键均开始。'};
  const storageKey='yumo.ui-preview.unified-choices.v1',clockKey='yumo.ui-preview.clock-choices.v1',themeKey='yumo.ui-preview.active-theme.v1';
  const appGallery=document.getElementById('app-gallery'),clockGallery=document.getElementById('clock-gallery');
  let theme=themes[0],generation=0;
  const parse=key=>{try{return JSON.parse(localStorage.getItem(key)||'{}')||{};}catch(_){return {};}};
  function validChoices(value,keys){return Object.fromEntries(Object.entries(value).filter(([k,v])=>keys.includes(k)&&themes.some(t=>t.id===v)));}
  let choices=validChoices(parse(storageKey),scenes),clockChoices=validChoices(parse(clockKey),C.faces);
  const legacyChoices=[];
  try{for(const key of ['yumo.ui-preview.choice.v1','yumo.ui-preview.layout-choice.v1']){const t=themes.find(t=>t.id===localStorage.getItem(key));if(t)legacyChoices.push(t.number+' '+t.name);}}catch(_){}
  function normalize(value){if(['accounts','secrets','pin'].includes(value))return 'vault';if(value==='wifiForm')return 'wifi';if(value==='apps')return 'home';return scenes.includes(value)?value:'home';}
  function store(){try{localStorage.setItem(storageKey,JSON.stringify(choices));localStorage.setItem(clockKey,JSON.stringify(clockChoices));}catch(_){}updateChoice();}
  function wholeSelected(){return scenes.every(key=>choices[key]===theme.id)&&C.faces.every(key=>clockChoices[key]===theme.id);}
  function updateChoice(){
    for(const card of cards.values()){
      const selected=choices[normalize(card.scene)]===card.theme.id;
      card.article.classList.toggle('selected',selected);card.choose.textContent=selected?'✓ 已设为候选':'设为候选';card.choose.setAttribute('aria-pressed',String(selected));
    }
    clockGallery.querySelectorAll('.clock-cell').forEach(cell=>{const clock=cell.querySelector('yumo-clock'),button=cell.querySelector('button'),selected=clockChoices[clock.face]===clock.getAttribute('theme');button.textContent=selected?'✓ 已选':'候选';button.setAttribute('aria-pressed',String(selected));});
    const whole=document.getElementById('choose-theme');whole.textContent=wholeSelected()?'✓ 本套已选':'整套设为候选';whole.setAttribute('aria-pressed',String(wholeSelected()));
    const summary=[...Object.entries(choices).map(([key,id])=>`${sceneNames[key]}：${themes.find(t=>t.id===id).number}`),...C.faces.flatMap((key,i)=>clockChoices[key]?[`${C.names[i]}表盘：${themes.find(t=>t.id===clockChoices[key]).number}`]:[])];
    document.getElementById('choice-summary').textContent=summary.length?summary.join(' · ')+'。只记录浏览器偏好，不更新设备。':'可整套设为候选，也可在各屏幕下单独选择；切换主题不会清除已有候选。';
    const previous=document.getElementById('previous-choices');previous.hidden=!legacyChoices.length;previous.textContent='此前候选已保留：'+legacyChoices.join(' / ')+'。可继续为各应用单独选择。';
  }
  function capture(card){
    const s=card.controller?.state;if(!s)return;
    for(const key of ['wifi','bt','motion','brightness','silent','ssid'])if(key in s)card.preferences[key]=s[key];
  }
  function dispose(card){
    capture(card);card.serial++;card.ready=false;card.controller?.dispose();card.controller=null;
    card.loads.forEach(cancel=>cancel());card.loads.clear();card.surface.shadowRoot?.replaceChildren();
  }
  function palette(screen,t){
    const vars={bg:t.bg,surface:t.surface,soft:t.edge,fg:t.fg,sub:t.muted,accent:t.accent,on:['tiles','orbit','terminal','matrix','ribbon'].includes(t.id)?t.bg:'#ffffff',line:t.edge,active:t.surface,radius:t.id==='terminal'?'0px':'7px','tile-radius':'7px','icon-bg':t.surface,danger:'#c94444'};
    Object.entries(vars).forEach(([k,v])=>screen.style.setProperty('--'+k,v));
  }
  function stylesheet(root,file,card){return new Promise((resolve,reject)=>{
    const link=document.createElement('link');link.rel='stylesheet';link.href=file;
    const cancel=()=>finish();let timer;
    function finish(error){clearTimeout(timer);link.onload=null;link.onerror=null;card.loads.delete(cancel);if(error)reject(error);else resolve();}
    card.loads.add(cancel);link.onload=()=>finish();link.onerror=()=>finish(Error('无法加载 '+file));
    timer=setTimeout(()=>finish(Error('加载超时：'+file)),8000);root.append(link);
  });}
  function showError(error){document.body.dataset.ready='error';document.getElementById('announcement').textContent=error.message+'，可点击“重置本套演示”重试。';console.error(error);}
  async function showCard(card,target){
    dispose(card);card.scene=target;const ticket=card.serial,t=card.theme;
    const root=card.surface.shadowRoot||card.surface.attachShadow({mode:'open'});
    const random=target==='random';
    const legacy=!random&&(Number(t.number)<=5||['wifi','bluetooth','wifiForm'].includes(target));
    const files=[random?'random.css':legacy?'styles.css':'layouts.css'];
    if(!random){if(!legacy&&Number(t.number)>10)files.push('extra-layouts.css');files.push('device-preview.css');}
    try{await Promise.all(files.map(file=>stylesheet(root,file,card)));}catch(error){if(ticket!==card.serial)return;throw error;}
    if(ticket!==card.serial)return;
    const screen=document.createElement('div');screen.className=legacy?'screen':'canvas';
    if(legacy){screen.dataset.theme=t.id;if(Number(t.number)>5)palette(screen,t);}else screen.dataset.kind=t.id;
    screen.tabIndex=0;screen.setAttribute('role','region');screen.setAttribute('aria-label',t.number+' '+t.name+' / '+sceneNames[card.slot]+'，240乘240交互预览');root.append(screen);
    const options={state:card.preferences,onScene:value=>{if(ticket!==card.serial)return;card.scene=value;card.selector.value=normalize(value);card.status.textContent=sceneNames[normalize(value)];updateChoice();},
      onRoute:value=>{
        if(value==='random'||random){showCard(card,value).catch(showError);return true;}
        if(legacy&&Number(t.number)>5&&['home','settings','vault','recorder','clock'].includes(value)){showCard(card,value).catch(showError);return true;}
        return false;
      }};
    card.controller=random?window.RandomPreview.mount(screen,t,options):legacy?L.mount(screen,L.themes.find(v=>v.id===t.id)||t,options):N.mount(screen,N.themes.find(v=>v.id===t.id),options);
    card.controller.navigate(target);card.controller.setVisible(card.inView);card.ready=true;
  }
  const observer=new IntersectionObserver(entries=>{for(const e of entries){const card=cards.get(e.target.dataset.slot);if(card){card.inView=e.isIntersecting;card.controller?.setVisible(e.isIntersecting);}}});
  for(const [index,slot] of appScenes.entries()){
    const article=document.createElement('article');article.className='ui-card';article.dataset.slot=slot;article.id='preview-'+slot;
    article.innerHTML=`<header class="card-head"><h3>${sceneNames[slot]}</h3><span>0${index+1} / 07</span></header><div class="stage"><div class="device"><div class="viewport"><div class="surface"></div></div></div></div><div class="card-info"><p>${hints[slot]}</p></div><label class="card-scene"><span>正在展示：<b data-status>${sceneNames[slot]}</b></span><select aria-label="${sceneNames[slot]}预览：单独切换应用">${scenes.map(key=>`<option value="${key}">${sceneNames[key]}</option>`).join('')}</select></label><div class="card-actions"><button data-choose aria-pressed="false">设为候选</button><button data-restore>还原本屏</button></div>`;
    appGallery.append(article);
    const card={slot,theme,article,surface:article.querySelector('.surface'),selector:article.querySelector('select'),status:article.querySelector('[data-status]'),choose:article.querySelector('[data-choose]'),preferences:{wifi:false,bt:false},scene:slot,serial:0,controller:null,inView:false,loads:new Set()};
    cards.set(slot,card);observer.observe(article);
    if(slot==='random'){
      const keys=document.createElement('div');keys.className='card-actions';
      for(const [key,label] of [['boot','模拟 BOOT'],['key','模拟 KEY']]){const button=document.createElement('button');button.textContent=label;button.addEventListener('click',()=>card.controller?.key?.(key));keys.append(button);}article.append(keys);
    }
    card.selector.addEventListener('change',()=>showCard(card,card.selector.value).catch(showError));
    card.choose.addEventListener('click',()=>{const key=normalize(card.scene);if(choices[key]===card.theme.id)delete choices[key];else choices[key]=card.theme.id;store();});
    article.querySelector('[data-restore]').addEventListener('click',()=>showCard(card,slot).catch(showError));
  }
  for(const t of themes){
    const option=document.createElement('option');option.value=t.id;option.textContent=t.number+' '+t.name+' · '+t.family;document.getElementById('theme-select').append(option);
    const button=document.createElement('button');button.className='theme-chip';button.dataset.theme=t.id;button.setAttribute('aria-pressed','false');button.setAttribute('aria-label',option.textContent);
    button.innerHTML=`<i class="swatch" aria-hidden="true"></i><small>${t.number}</small><strong>${t.name}</strong><span>${t.family}</span>`;button.querySelector('i').style.background=t.accent;
    button.addEventListener('click',()=>requestTheme(t.id));document.getElementById('theme-list').append(button);
  }
  function renderClocks(){
    const t=theme;
    clockGallery.innerHTML=`<article class="clock-section" data-theme="${t.id}"><div class="clock-row">${C.faces.map((face,i)=>`<div class="clock-cell" data-original-face="${face}"><div class="stage"><div class="device"><div class="viewport"><yumo-clock theme="${t.id}" face="${face}"></yumo-clock></div></div></div><div class="clock-caption"><div><strong>${C.names[i]}表盘</strong><p>${C.details[t.id][i]}</p></div><button data-choose-clock="${t.id}" aria-pressed="false">候选</button></div></div>`).join('')}</div></article>`;
    updateChoice();
  }
  async function showTheme(id){
    const next=themes.find(t=>t.id===id);if(!next)return;
    document.body.dataset.ready='false';const ticket=++generation;
    cards.forEach(dispose);clockGallery.replaceChildren();theme=next;
    try{localStorage.setItem(themeKey,id);}catch(_){}
    document.body.dataset.theme=id;document.documentElement.style.setProperty('--theme-accent',theme.accent);
    document.getElementById('theme-select').value=id;document.getElementById('theme-position').textContent=theme.number+' / 15';
    document.querySelectorAll('.theme-chip').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.theme===id)));
    document.getElementById('theme-number').textContent='THEME / '+theme.number+' OF 15';
    document.getElementById('theme-name').textContent=theme.name+' · '+theme.family;
    const meta=L.themes.find(t=>t.id===id)||N.themes.find(t=>t.id===id);
    document.getElementById('theme-description').textContent=meta.description||meta.subtitle;
    document.getElementById('theme-palette').replaceChildren(...[theme.bg,theme.surface,theme.accent,theme.fg].map(color=>{const dot=document.createElement('i');dot.style.background=color;return dot;}));
    let notes=document.querySelector('.theme-notes');if(!notes){notes=document.createElement('p');notes.className='theme-notes';appGallery.after(notes);}
    notes.textContent=(meta.try||'点应用进入，点击列表项深入；网络输入复用共享键盘。')+' '+(meta.tradeoff||'保留紧凑双列与原有操作方式，只改变视觉组织。');
    renderClocks();
    await Promise.all([...cards.values()].map(card=>{card.theme=theme;card.article.dataset.theme=id;card.preferences={wifi:false,bt:false};return showCard(card,card.slot);}));
    if(ticket===generation){document.body.dataset.ready='true';updateChoice();document.getElementById('announcement').textContent=theme.number+' '+theme.name+'主题已加载：七个应用场景和三种表盘。';}
  }
  function remembered(){try{const id=localStorage.getItem(themeKey);if(themes.some(t=>t.id===id))return id;}catch(_){}return themes[0].id;}
  function requestTheme(id){if(!themes.some(t=>t.id===id))return;if(location.hash==='#theme='+id)showTheme(id).catch(showError);else location.hash='theme='+id;}
  function jump(slot){const target=slot==='clock'?document.getElementById('clock-area'):cards.get(slot)?.article;target?.scrollIntoView({block:'start'});}
  async function route(){
    const hash=location.hash.slice(1),aliases={original:'clear',new:'tiles',latest:'tabs'},id=hash.startsWith('theme=')?hash.slice(6):aliases[hash]||remembered();
    await showTheme(themes.some(t=>t.id===id)?id:themes[0].id);
    if(scenes.includes(hash)||hash==='clocks')jump(hash==='clocks'?'clock':hash);
  }
  document.getElementById('theme-select').addEventListener('change',event=>requestTheme(event.target.value));
  for(const [id,delta] of [['previous-theme',-1],['next-theme',1]])document.getElementById(id).addEventListener('click',()=>requestTheme(themes[(themes.indexOf(theme)+delta+themes.length)%themes.length].id));
  document.querySelectorAll('[data-jump]').forEach(button=>button.addEventListener('click',()=>jump(button.dataset.jump)));
  window.addEventListener('hashchange',()=>route().catch(showError));
  document.getElementById('scale').addEventListener('change',event=>{const zoom=event.target.value==='1.5'?'1.5':'1';document.documentElement.style.setProperty('--zoom',zoom);document.body.classList.toggle('zoomed',zoom==='1.5');});
  document.getElementById('reset').addEventListener('click',()=>showTheme(theme.id).catch(showError));
  document.getElementById('capability-toggle').addEventListener('click',event=>{const block=document.getElementById('capabilities');block.hidden=!block.hidden;event.currentTarget.setAttribute('aria-expanded',String(!block.hidden));});
  document.getElementById('choose-theme').addEventListener('click',()=>{if(wholeSelected()){scenes.forEach(key=>delete choices[key]);C.faces.forEach(key=>delete clockChoices[key]);}else{scenes.forEach(key=>choices[key]=theme.id);C.faces.forEach(key=>clockChoices[key]=theme.id);}store();});
  document.getElementById('clear-choice').addEventListener('click',()=>{choices={};clockChoices={};legacyChoices.length=0;try{localStorage.removeItem('yumo.ui-preview.choice.v1');localStorage.removeItem('yumo.ui-preview.layout-choice.v1');}catch(_){}store();});
  clockGallery.addEventListener('click',event=>{const button=event.target.closest('[data-choose-clock]');if(!button)return;const clock=button.closest('.clock-cell').querySelector('yumo-clock');if(clockChoices[clock.face]===button.dataset.chooseClock)delete clockChoices[clock.face];else clockChoices[clock.face]=button.dataset.chooseClock;store();});
  clockGallery.addEventListener('clock-face-change',event=>{const clock=event.target,cell=clock.closest('.clock-cell'),i=C.faces.indexOf(clock.face);cell.querySelector('strong').textContent=C.names[i]+'表盘';cell.querySelector('.clock-caption p').textContent=C.details[clock.getAttribute('theme')][i];updateChoice();});
  document.getElementById('reset-faces').addEventListener('click',renderClocks);
  document.getElementById('time-mode').addEventListener('change',event=>C.setMode(event.target.value));
  const times={sample:{year:2026,month:9,day:23,hour:10,minute:8,second:36},wide:{year:2026,month:12,day:31,hour:23,minute:59,second:58},narrow:{year:2026,month:11,day:11,hour:11,minute:11,second:11},midnight:{year:2026,month:12,day:31,hour:23,minute:59,second:59},leap:{year:2028,month:2,day:29,hour:23,minute:59,second:59}};
  document.getElementById('test-time').addEventListener('change',event=>{const mode=document.getElementById('time-mode');if(mode.value==='local')mode.value='demo';C.setMode(mode.value);C.setTime(times[event.target.value]);});
  document.addEventListener('preview-message',event=>document.getElementById('announcement').textContent=event.detail);
  window.addEventListener('pagehide',()=>{generation++;cards.forEach(dispose);clockGallery.replaceChildren();});
  window.addEventListener('pageshow',event=>{if(event.persisted)showTheme(theme.id).catch(showError);});
  window.UnifiedPreview={cards,showTheme,showCard,get theme(){return theme.id;}};
  route().catch(showError);
})();
