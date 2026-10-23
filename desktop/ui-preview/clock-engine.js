/* 240px clock study. Canvas is only the browser renderer, not firmware code.
 * Every face maps to existing LVGL labels, rounded rectangles, circles, arcs,
 * and persistent lines. No SVG decoder, custom TTF, blur or GPU is required.
 */
'use strict';
(() => {
  const THEMES = [
    ['clear','01','澄蓝','轻快卡片','#eef2f7','#ffffff','#24334a','#2868e8','#72829a','#dce4ee'],
    ['sage','02','青苔','柔和色阶','#edf1e6','#e1e8d7','#2b4234','#476c50','#6d7d69','#ccd7c1'],
    ['night','03','夜航','深色仪表','#101419','#1c242f','#e8f0fa','#83bcff','#9baac0','#344354'],
    ['warm','04','砂岩','温暖留白','#f6f0e7','#fffcf6','#594737','#b95435','#9b8770','#e4d6c4'],
    ['mono','05','墨线','精密工具','#f1f2ee','#f9faf6','#24362d','#344f3d','#707b6e','#b9c7b3'],
    ['tiles','06','拼贴','信息磁贴','#0e1b26','#203443','#f2f7fb','#c6f26d','#8499ab','#3e5260'],
    ['orbit','07','轨道','径向菜单','#18191c','#2d2e30','#f4f3ef','#efbf69','#92918b','#555044'],
    ['focus','08','一页','滑动卡片','#f1efeb','#e6e2d9','#282723','#ec5c34','#8e8980','#c6c1b6'],
    ['rail','09','分舱','侧栏工作台','#f5f6fa','#e9edf5','#303951','#566eca','#8690a4','#cfd6e4'],
    ['terminal','10','游标','复古终端','#101e1b','#203b2b','#b8d6bd','#d5ef8d','#627e6c','#405b42'],
    ['tabs','11','索引','标签抽屉','#f2eff8','#e5ddf0','#352d49','#7154b2','#77688c','#c9bfdc'],
    ['folio','12','票签','纸质目录','#f5f1e6','#e8e3d4','#282824','#353d38','#757466','#b8b8a5'],
    ['matrix','13','方阵','六键分页','#071e28','#123745','#dcf8f1','#5de1c2','#8bbaae','#295764'],
    ['ribbon','14','层叠','拇指卡组','#211f36','#35314d','#f0eaff','#c5bbff','#a7a0c0','#514b6b'],
    ['split','15','分镜','双窗索引','#f4eddf','#e7dcc7','#413726','#956525','#857453','#c5b596']
  ].map(([id,number,name,family,bg,surface,fg,accent,muted,edge])=>({id,number,name,family,bg,surface,fg,accent,muted,edge}));
  const FACES=['digital','analog','hybrid'];
  const FACE_NAMES=['数字','指针','混合'];
  const DETAILS={
    clear:['横向大数字 / 秒数胶囊','标准整圆 / 蓝色四极刻度','上部小表盘 / 下部数字'],
    sage:['上下分区 / 柔和色面','无数字刻度 / 同心圆','圆形小表盘 / 底部信息块'],
    night:['左右大数字 / 仪表分区','方形外框 / 稀疏刻度','顶部信息带 / 大表盘'],
    warm:['四格数字 / 印刷式日期','全数字盘 / 暖色日历窗','左侧表盘 / 右侧时分'],
    mono:['框线数字 / 精密网格','直角边界 / 十字辅助线','上下分栏 / 工具表盘'],
    tiles:['三块磁贴 / 主次色面','大表盘 / 底部日期磁贴','左大表盘 / 右侧秒数块'],
    orbit:['双行数字 / 外缘分钟轨道','双层圆轨 / 金色刻度','偏心双区 / 下方数字'],
    focus:['左侧小时 / 右下分钟','不对称数字 / 橙色秒针','大数字与小表盘对角排布'],
    rail:['窄日期栏 / 时分内容区','左侧日历 / 右侧表盘','三行信息栏 / 下部表盘'],
    terminal:['等宽列布局 / 矩形字段','点线表盘 / 反白时标','上部游标时间 / 下部表盘'],
    tabs:['页签日历 / 横向主时钟','页签标题 / 紧凑紫色圆盘','左表盘 / 右侧时间抽屉'],
    folio:['印刷大数字 / 双横线','全数字刻度 / 顶部票号日期','双列票签 / 左数字右表盘'],
    matrix:['四个独立数字格 / 底部秒数','方块时标 / 六十刻度','左上大表盘 / 下方数字格'],
    ribbon:['错位色面 / 上时下分','中部亮色带 / 悬浮刻度','顶部宽数字卡 / 底部小表盘'],
    split:['左侧大数字 / 右侧日历','左侧表盘 / 右侧日期专栏','左侧时分 / 右侧小圆盘']
  };
  const fonts=window.ClockFonts;
  const pad=n=>String(n).padStart(2,'0');
  const week=['SUN','MON','TUE','WED','THU','FRI','SAT'];
  const cnWeek=['日','一','二','三','四','五','六'];
  const date=n=>`${n.year}.${pad(n.month)}.${pad(n.day)}`;
  const hm=n=>`${pad(n.hour)}:${pad(n.minute)}`;
  const hms=n=>`${hm(n)}:${pad(n.second)}`;
  const seconds=n=>pad(n.second)+' SEC';
  const sample=Date.UTC(2026,8,23,10,8,36);
  let mode='demo',epoch=sample,started=Date.now(),timer=null;
  const instances=new Set(), backgroundCache=new Map(), glyphCache=new Map();
  const audit={fontMisses:[],renders:0};

  function civil() {
    const d=mode==='local'?new Date():new Date(epoch+(mode==='demo'?Date.now()-started:0));
    const get=name=>d[(mode==='local'?'get':'getUTC')+name]();
    return {year:get('FullYear'),month:get('Month')+1,day:get('Date'),weekday:get('Day'),hour:get('Hours'),minute:get('Minutes'),second:get('Seconds')};
  }
  function rgb565(hex) {
    const n=parseInt(hex.slice(1),16),r=(n>>19)&31,g=(n>>10)&63,b=(n>>3)&31;
    return '#'+[((r<<3)|(r>>2)),((g<<2)|(g>>4)),((b<<3)|(b>>2))].map(v=>v.toString(16).padStart(2,'0')).join('');
  }
  function glyph(font,char,color) {
    const key=font+'|'+char+'|'+color;
    if(glyphCache.has(key)) return glyphCache.get(key);
    const f=fonts[font],g=f?.glyphs[char];
    if(!g) { audit.fontMisses.push([font,char]); throw Error(`Clock glyph missing: ${font}/${char}`); }
    const [advance,w,h,x,y,packed]=g;
    let image=null;
    if(w&&h) {
      const bytes=Uint8Array.from(atob(packed),c=>c.charCodeAt(0)),bits=f.bpp,max=(1<<bits)-1;
      image=document.createElement('canvas');image.width=w;image.height=h;
      const ctx=image.getContext('2d'),pixels=ctx.createImageData(w,h),n=parseInt(color.slice(1),16);
      for(let i=0;i<w*h;i++) {
        const coverage=(bytes[Math.floor(i*bits/8)]>>(8-bits-(i*bits%8)))&max;
        pixels.data.set([n>>16,(n>>8)&255,n&255,Math.round(255*coverage/max)],i*4);
      }
      ctx.putImageData(pixels,0,0);
    }
    const result={image,advance:Math.round(advance/16),w,h,x,y,height:f.height,baseline:f.baseline};
    if(glyphCache.size>=2048) glyphCache.clear();
    glyphCache.set(key,result);return result;
  }
  function width(text,font,fixed=false) {
    return [...String(text)].reduce((v,ch)=>v+(fixed&&font===84?(ch===':'?20:48):Math.round(fonts[font].glyphs[ch][0]/16)),0);
  }
  function drawText(ctx,text,x,y,font,color,anchor='left',fixed=false) {
    const w=width(text,font,fixed); let pen=Math.round(x-(anchor==='center'?w/2:anchor==='right'?w:0));
    for(const char of String(text)) {
      const g=glyph(font,char,color),cell=fixed&&font===84?(char===':'?20:48):g.advance;
      if(g.image) ctx.drawImage(g.image,pen+g.x+(fixed&&font===84?Math.floor((cell-g.advance)/2):0),Math.round(y)+g.height-g.baseline-g.h-g.y);
      pen+=cell;
    }
    return {x:Math.round(x-(anchor==='center'?w/2:anchor==='right'?w:0)),y,w,h:fonts[font].height,text:String(text),font};
  }
  const point=(x,y,r,deg)=>({x:x+Math.sin(deg*Math.PI/180)*r,y:y-Math.cos(deg*Math.PI/180)*r});
  function scene(t,face) {
    const a=[],dyn=[]; const push=(cmd,dynamic=false)=>(dynamic?dyn:a).push(cmd);
    const rect=(x,y,w,h,color,r=0,outline=false)=>push({type:'rect',x,y,w,h,color,r,outline});
    const circle=(x,y,r,color,stroke=0)=>push({type:'circle',x,y,r,color,stroke});
    const line=(x,y,x2,y2,color,w=1)=>push({type:'line',x,y,x2,y2,color,w});
    const label=(v,x,y,font=14,color=t.fg,anchor='left',fixed=false)=>push({type:'text',v,x,y,font,color,anchor,fixed},typeof v==='function');
    const hands=(x,y,r,wide=false)=>push({type:'hands',x,y,r,fg:t.fg,accent:t.accent,wide},true);
    const ticks=(x,y,r,count=60,cardinalOnly=false)=>{
      for(let i=0;i<count;i++) {
        const major=i%(count/12)===0;
        if(cardinalOnly&&!major) continue;
        const p1=point(x,y,r-(major?7:3),i*360/count),p2=point(x,y,r,i*360/count);
        line(p1.x,p1.y,p2.x,p2.y,major?t.fg:t.edge,major?2:1);
      }
    };
    const dial=(x,y,r,{all=false,numbers=true,rim=true,small=false}={})=>{
      if(rim)circle(x,y,r,t.edge,1);
      ticks(x,y,r-3);
      if(numbers) for(const n of all?[1,2,3,4,5,6,7,8,9,10,11,12]:[12,3,6,9]) {
        const p=point(x,y,r-(small?16:23),(n%12)*30);
        label(String(n),p.x,p.y-(small?6:9),small?12:18,t.fg,'center');
      }
    };
    const bar=(x,y,w,h,color=t.accent)=>push({type:'bar',x,y,w,h,color},true);
    rect(0,0,240,240,t.bg);
    const id=t.id;
    if(face==='digital') {
      switch(id) {
        case 'tabs':
          rect(0,0,240,41,t.surface);rect(6,4,80,37,t.bg,7);label(n=>week[n.weekday],46,13,18,t.accent,'center');label(date,231,15,12,t.muted,'right');
          label('TIME / 24H',12,63,12,t.muted);label(hm,120,88,84,t.fg,'center',true);line(12,160,228,160,t.edge);
          rect(12,179,216,48,t.accent,8);label('SECONDS',23,196,12,t.bg);label(n=>pad(n.second),214,184,32,t.bg,'right');break;
        case 'folio':
          label('YUMO / TIME EDITION',8,8,12,t.fg);line(8,31,232,31,t.fg,3);label(date,8,44,14,t.muted);label(n=>week[n.weekday],232,44,14,t.muted,'right');
          label(hm,120,83,84,t.fg,'center',true);line(8,158,232,158,t.fg,2);line(8,164,232,164,t.fg);
          label('NO.',10,194,18,t.muted);label(n=>pad(n.second),230,181,40,t.fg,'right');label('24 HOUR / RAIN & INK',10,226,10,t.muted);break;
        case 'matrix':
          for(let i=0;i<4;i++){const x=(i%2)*121,y=Math.floor(i/2)*98;rect(x,y,119,96,t.surface,4);label(n=>hm(n).replace(':','')[i],x+60,y+18,84,i<2?t.fg:t.accent,'center',true);}
          label(date,7,203,12,t.muted);label(n=>week[n.weekday],7,222,10,t.accent);label(seconds,232,211,18,t.accent,'right');break;
        case 'ribbon':
          label(date,8,10,12,t.muted);label(n=>week[n.weekday],232,10,12,t.accent,'right');rect(0,39,216,80,t.surface,9);label(n=>pad(n.hour),14,48,84,t.fg,'left',true);label('HOUR',199,76,12,t.muted,'right');
          rect(24,125,216,80,t.accent,9);label('MIN',37,155,12,t.bg);label(n=>pad(n.minute),226,132,84,t.bg,'right',true);label(seconds,120,220,12,t.muted,'center');break;
        case 'split':
          rect(155,0,85,240,t.accent);label('HOUR',12,8,10,t.muted);label(n=>pad(n.hour),76,30,84,t.fg,'center',true);line(9,105,145,105,t.edge);label(n=>pad(n.minute),76,117,84,t.fg,'center',true);label(seconds,77,211,14,t.muted,'center');
          label(n=>String(n.year),197,16,18,t.bg,'center');label(n=>pad(n.month),197,57,32,t.bg,'center');label(n=>pad(n.day),197,105,32,t.bg,'center');label(n=>week[n.weekday],197,172,18,t.bg,'center');label('YUMO',197,218,10,t.bg,'center');break;
        case 'clear':
          label(n=>week[n.weekday],12,14,24,t.accent);label('YUMO',228,21,12,t.muted,'right');
          label(date,12,48,14,t.muted);rect(0,78,240,85,t.surface,7);
          label(hm,120,90,84,t.fg,'center',true);rect(73,183,94,35,t.surface,16);
          label(seconds,120,191,18,t.accent,'center');bar(10,233,220,3);break;
        case 'sage':
          rect(0,0,240,114,t.surface,18);label(n=>pad(n.hour),22,31,84,t.fg,'left',true);
          label('HOUR',215,17,10,t.muted,'right');label(n=>week[n.weekday],215,61,18,t.accent,'right');
          rect(0,116,240,124,t.edge,18);label(n=>pad(n.minute),216,136,84,t.accent,'right',true);
          label('MIN',17,129,10,t.muted);label(n=>pad(n.second),17,157,24,t.fg);label(date,18,214,14,t.fg);break;
        case 'night':
          label(date,8,10,14,t.muted);label(n=>week[n.weekday],232,10,14,t.accent,'right');
          rect(0,43,119,130,t.surface,8);rect(121,43,119,130,t.surface,8);
          label('HOURS',60,58,10,t.muted,'center');label('MINUTES',180,58,10,t.muted,'center');
          label(n=>pad(n.hour),60,91,84,t.fg,'center',true);label(n=>pad(n.minute),180,91,84,t.accent,'center',true);
          label('SECONDS',10,196,12,t.muted);label(n=>pad(n.second),230,181,40,t.fg,'right');bar(10,230,220,3);break;
        case 'warm':
          label('Y U M O',120,15,14,t.accent,'center');label(date,120,44,14,t.muted,'center');
          for(let i=0;i<4;i++){rect(2+i*60,85,56,90,t.surface,6);rect(2+i*60,85,56,90,t.edge,6,true);label(n=>hm(n).replace(':','')[i],30+i*60,99,84,i<2?t.fg:t.accent,'center',true);}
          label(n=>'星期'+cnWeek[n.weekday],9,207,'cjk16',t.fg);label(seconds,231,208,14,t.accent,'right');break;
        case 'mono':
          rect(0,0,240,240,t.edge,0,true);line(0,37,240,37,t.edge);line(0,186,240,186,t.edge);
          label('TIME / 24H',9,10,14);label(hm,120,77,84,t.fg,'center',true);
          label(date,9,158,14,t.muted);label(n=>week[n.weekday],230,158,14,t.accent,'right');
          label('SEC',9,204,14,t.muted);label(n=>pad(n.second),231,192,32,t.fg,'right');break;
        case 'tiles':
          rect(0,0,143,158,t.accent,10);label('HOURS',10,10,12,t.bg);label(n=>pad(n.hour),70,59,84,t.bg,'center',true);
          rect(147,0,93,76,t.surface,9);label(n=>pad(n.second),193,12,40,t.fg,'center');label('SEC',193,54,10,t.muted,'center');
          rect(147,80,93,78,t.surface,9);label(n=>week[n.weekday],193,94,18,t.accent,'center');label(n=>pad(n.day),193,120,24,t.fg,'center');
          rect(0,162,240,78,t.surface,9);label('MIN',12,181,12,t.muted);label(n=>pad(n.minute),227,170,84,t.fg,'right',true);label(n=>n.year+'.'+pad(n.month),10,212,12,t.muted);break;
        case 'orbit':
          circle(120,120,116,t.edge,1);ticks(120,120,113);label(n=>pad(n.hour),120,39,84,t.fg,'center',true);
          label(n=>pad(n.minute),120,111,84,t.accent,'center',true);label(seconds,120,182,14,t.muted,'center');label(date,120,207,12,t.muted,'center');break;
        case 'focus':
          label(n=>week[n.weekday],9,12,18,t.accent);label('24H',230,16,12,t.muted,'right');line(8,40,232,40,t.edge);
          label(n=>pad(n.hour),8,58,84,t.fg,'left',true);label(n=>pad(n.minute),232,127,84,t.accent,'right',true);
          line(126,67,111,165,t.edge,2);label(seconds,9,166,12,t.muted);label(date,9,218,14,t.fg);break;
        case 'rail':
          rect(0,0,51,240,t.accent);label(n=>String(n.year),25,13,14,t.surface,'center');label(n=>pad(n.month),25,71,24,t.surface,'center');label(n=>pad(n.day),25,108,24,t.surface,'center');label(n=>week[n.weekday],25,188,12,t.surface,'center');
          label('HOURS',67,13,12,t.muted);label(n=>pad(n.hour),143,39,84,t.fg,'center',true);line(62,110,231,110,t.edge);
          label(n=>pad(n.minute),143,121,84,t.accent,'center',true);label(seconds,144,206,18,t.muted,'center');break;
        case 'terminal':
          label('YUMO',6,6,12,t.accent);label('24H',234,6,12,t.muted,'right');line(5,27,235,27,t.edge);
          label('DATE',7,42,12,t.muted);label(date,232,42,14,t.fg,'right');rect(0,81,240,70,t.surface);
          label(hm,120,86,84,t.accent,'center',true);label('DAY',7,169,12,t.muted);label(n=>week[n.weekday],232,169,14,t.fg,'right');
          rect(5,203,45,27,t.accent);label('SEC',27,209,12,t.bg,'center');label(n=>pad(n.second),231,198,24,t.accent,'right');break;
      }
    } else if(face==='analog') {
      switch(id) {
        case 'tabs':rect(0,0,240,30,t.surface);label(n=>week[n.weekday],10,8,12,t.accent);label(date,231,8,12,t.muted,'right');circle(120,134,102,t.surface);dial(120,134,102,{small:true,rim:false});hands(120,134,85);break;
        case 'folio':label(date,8,7,12,t.fg);label('NO. 12',232,7,10,t.muted,'right');line(5,26,235,26,t.fg,2);dial(120,134,102,{all:true,small:true,rim:false});label('YUMO',120,166,10,t.muted,'center');hands(120,134,75,true);break;
        case 'matrix':ticks(120,120,116);for(let n=0;n<12;n++){const p=point(120,120,100,n*30);rect(p.x-4,p.y-4,8,8,n%3===0?t.accent:t.edge,1);}rect(76,158,88,30,t.surface,3);label(n=>week[n.weekday]+' '+pad(n.day),120,166,12,t.accent,'center');label('YUMO',120,62,12,t.muted,'center');hands(120,120,89);break;
        case 'ribbon':rect(0,87,240,66,t.surface);ticks(120,120,115);circle(120,120,88,t.edge,1);label('12',120,15,24,t.accent,'center');label('6',120,198,24,t.accent,'center');label(n=>pad(n.month)+'.'+pad(n.day),120,171,12,t.muted,'center');hands(120,120,92,true);break;
        case 'split':rect(181,0,59,240,t.accent);dial(90,120,86,{small:true,rim:false});hands(90,120,67);label(n=>String(n.year),210,12,14,t.bg,'center');label(n=>pad(n.month),210,63,24,t.bg,'center');label(n=>pad(n.day),210,109,24,t.bg,'center');label(n=>week[n.weekday],210,171,12,t.bg,'center');label('YUMO',210,216,10,t.bg,'center');break;
        case 'clear':dial(120,120,118,{rim:false});label('YUMO',120,69,12,t.accent,'center');label(n=>week[n.weekday]+' '+pad(n.day),120,164,14,t.muted,'center');hands(120,120,105);break;
        case 'sage':circle(120,120,117,t.surface);circle(120,120,96,t.edge,1);dial(120,120,117,{numbers:false,rim:false});label('Y U M O',120,66,12,t.accent,'center');label(n=>pad(n.month)+' / '+pad(n.day),120,163,14,t.accent,'center');hands(120,120,102,true);break;
        case 'night':rect(1,1,238,238,t.edge,8,true);ticks(120,120,111,60,true);for(const [num,x,y] of [['12',120,12],['3',220,108],['6',120,207],['9',20,108]])label(num,x,y,18,t.muted,'center');label('YUMO',120,62,12,t.accent,'center');label(n=>week[n.weekday]+' '+pad(n.day),120,167,14,t.muted,'center');hands(120,120,98);break;
        case 'warm':dial(120,120,118,{all:true});rect(82,160,76,27,t.surface,3);label(n=>week[n.weekday]+' '+pad(n.day),120,167,12,t.accent,'center');label('YUMO',120,72,12,t.muted,'center');hands(120,120,88,true);break;
        case 'mono':rect(1,1,238,238,t.fg,0,true);dial(120,120,112,{rim:false});line(52,120,188,120,t.edge);line(120,52,120,188,t.edge);label('YUMO',120,70,10,t.muted,'center');label(n=>pad(n.month)+'.'+pad(n.day),120,165,12,t.muted,'center');hands(120,120,90);break;
        case 'tiles':circle(120,104,99,t.surface);dial(120,104,99,{numbers:true,small:true,rim:false});rect(2,207,236,31,t.accent,6);label(n=>week[n.weekday]+'   '+date(n),120,215,14,t.bg,'center');hands(120,104,82,true);break;
        case 'orbit':circle(120,120,117,t.edge,1);circle(120,120,105,t.surface,1);ticks(120,120,115);circle(120,120,80,t.edge,1);label('YUMO',120,65,12,t.accent,'center');label(n=>week[n.weekday]+' '+pad(n.day),120,164,14,t.muted,'center');hands(120,120,100);break;
        case 'focus':ticks(120,119,115,60,true);label('12',120,10,32,t.fg,'center');label('3',218,103,24,t.accent,'center');label('6',120,204,24,t.muted,'center');label('9',20,103,24,t.muted,'center');label('YUMO',120,71,10,t.muted,'center');label(n=>week[n.weekday]+' '+pad(n.day),120,160,12,t.muted,'center');hands(120,119,89);break;
        case 'rail':rect(0,0,46,240,t.surface);label('YUMO',23,11,10,t.accent,'center');label(n=>week[n.weekday],23,56,12,t.accent,'center');label(n=>pad(n.day),23,88,24,t.fg,'center');label(n=>String(n.year),23,184,12,t.muted,'center');label(n=>pad(n.month),23,204,14,t.accent,'center');dial(143,120,94,{small:true});hands(143,120,78);break;
        case 'terminal':label(date,6,5,10,t.muted);label(n=>week[n.weekday],234,5,10,t.accent,'right');dial(120,130,108,{numbers:false});for(const num of [12,3,6,9]){const p=point(120,130,84,num*30);rect(p.x-14,p.y-9,28,19,t.accent);label(String(num),p.x,p.y-7,12,t.bg,'center');}label('YUMO',120,81,10,t.accent,'center');hands(120,130,96);break;
      }
    } else {
      switch(id) {
        case 'tabs':rect(0,0,240,35,t.surface);label(date,9,11,12,t.muted);label(n=>week[n.weekday],231,11,12,t.accent,'right');dial(79,130,74,{small:true,rim:false});hands(79,130,58);rect(159,45,81,169,t.surface,7);label(n=>pad(n.hour),199,65,40,t.accent,'center');label(n=>pad(n.minute),199,124,40,t.accent,'center');label(seconds,120,222,12,t.muted,'center');break;
        case 'folio':label('YUMO / DUAL TIME',8,8,12,t.fg);line(8,30,232,30,t.fg,3);label(n=>pad(n.hour),45,67,48,t.fg,'center');label(n=>pad(n.minute),45,124,48,t.fg,'center');line(90,45,90,196,t.edge);dial(167,121,69,{small:true});hands(167,121,52);line(8,207,232,207,t.fg);label(date,8,220,12,t.muted);label(seconds,232,220,12,t.muted,'right');break;
        case 'matrix':rect(0,0,163,164,t.surface,3);dial(81,82,77,{numbers:false,rim:false});hands(81,82,65);rect(167,0,73,164,t.surface,3);label('SEC',204,13,12,t.muted,'center');label(n=>pad(n.second),204,42,40,t.accent,'center');label(n=>week[n.weekday],204,104,12,t.fg,'center');label(n=>pad(n.day),204,130,18,t.accent,'center');rect(0,168,240,72,t.surface,3);label(hm,9,172,40,t.accent);label(n=>String(n.year),232,178,12,t.muted,'right');label(n=>pad(n.month)+'.'+pad(n.day),232,202,12,t.muted,'right');label('24H',10,224,10,t.muted);break;
        case 'ribbon':rect(0,0,228,57,t.accent,8);label(hm,10,6,40,t.bg);label(n=>pad(n.second),216,14,24,t.bg,'right');rect(12,64,228,31,t.surface,6);label(date,24,73,12,t.muted);label(n=>week[n.weekday],227,73,12,t.accent,'right');dial(120,168,68,{numbers:false,rim:false});hands(120,168,59);break;
        case 'split':rect(0,0,84,240,t.accent);label('HOUR',42,12,10,t.bg,'center');label(n=>pad(n.hour),42,40,40,t.bg,'center');label('MIN',42,101,10,t.bg,'center');label(n=>pad(n.minute),42,130,40,t.bg,'center');label(n=>pad(n.second),42,202,18,t.bg,'center');dial(163,108,72,{small:true,rim:false});hands(163,108,55);label(n=>String(n.year),163,193,14,t.muted,'center');label(n=>pad(n.month)+'.'+pad(n.day),163,218,14,t.accent,'center');break;
        case 'clear':dial(120,80,77,{small:true});hands(120,80,63);rect(0,161,240,79,t.surface,7);label(hm,120,166,48,t.accent,'center');label(date,120,219,14,t.muted,'center');break;
        case 'sage':rect(0,0,240,162,t.surface,18);dial(120,81,76,{numbers:false,rim:false});hands(120,81,66,true);label(hm,14,177,40,t.accent);label(n=>pad(n.second),223,181,24,t.fg,'right');label(date,15,221,12,t.muted);break;
        case 'night':rect(0,0,240,53,t.surface,6);label(hm,9,6,32,t.accent);label(seconds,231,11,14,t.muted,'right');label(date,231,32,10,t.muted,'right');dial(120,147,89,{small:true});hands(120,147,74);break;
        case 'warm':label('Y U M O',12,13,12,t.accent);label(date,229,14,12,t.muted,'right');dial(79,119,76,{small:true});hands(79,119,59,true);line(159,53,159,188,t.edge);label(n=>pad(n.hour),199,73,40,t.fg,'center');label(n=>pad(n.minute),199,128,40,t.accent,'center');label(n=>week[n.weekday]+' / '+seconds(n),120,218,14,t.muted,'center');break;
        case 'mono':rect(1,1,238,238,t.edge,0,true);line(1,73,239,73,t.edge);label(hm,9,9,40,t.fg);label(seconds,231,15,12,t.accent,'right');label(date,9,54,12,t.muted);dial(120,156,79,{small:true,rim:false});hands(120,156,66);break;
        case 'tiles':rect(0,0,166,166,t.surface,9);dial(83,83,79,{small:true,rim:false});hands(83,83,63);rect(170,0,70,166,t.accent,9);label('SEC',205,14,12,t.bg,'center');label(n=>pad(n.second),205,56,40,t.bg,'center');label(n=>week[n.weekday],205,126,12,t.bg,'center');rect(0,170,240,70,t.surface,9);label(hm,9,175,40,t.fg);label(n=>pad(n.month)+'.'+pad(n.day),231,178,12,t.accent,'right');label(n=>String(n.year),231,209,12,t.muted,'right');break;
        case 'orbit':circle(120,86,81,t.edge,1);dial(120,86,73,{numbers:false,rim:false});hands(120,86,66);label(hm,120,176,40,t.accent,'center');label(date,120,223,12,t.muted,'center');break;
        case 'focus':label(n=>pad(n.hour),8,4,84,t.fg,'left',true);label(n=>week[n.weekday],230,15,14,t.accent,'right');label(n=>pad(n.month)+'.'+pad(n.day),230,39,12,t.muted,'right');dial(164,136,72,{numbers:false,rim:false});hands(164,136,65);label(n=>pad(n.minute),8,146,48,t.accent);label(seconds,8,202,12,t.muted);label(n=>String(n.year),8,226,10,t.muted);break;
        case 'rail':rect(0,0,240,47,t.surface);label(hm,9,5,32,t.accent);label(seconds,231,7,12,t.muted,'right');rect(0,48,65,192,t.surface);label('DATE',32,66,10,t.muted,'center');label(n=>pad(n.month),32,90,24,t.fg,'center');label(n=>pad(n.day),32,121,24,t.accent,'center');label(n=>week[n.weekday],32,177,12,t.fg,'center');label(n=>String(n.year),32,211,12,t.muted,'center');dial(153,145,83,{small:true});hands(153,145,67);break;
        case 'terminal':label('YUMO',6,6,10,t.accent);label(date,234,6,10,t.muted,'right');rect(3,24,234,49,t.accent);label(hm,12,30,32,t.bg);label(n=>pad(n.second),230,35,24,t.bg,'right');dial(120,155,79,{numbers:false,rim:false});label('12',120,87,12,t.muted,'center');label('6',120,207,12,t.muted,'center');hands(120,155,67);break;
      }
    }
    return {static:a,dynamic:dyn};
  }

  function command(ctx,c,n,report) {
    const color=rgb565(c.color||'#000000'); ctx.fillStyle=color;ctx.strokeStyle=color;ctx.lineWidth=1;
    switch(c.type) {
      case 'rect':ctx.beginPath();ctx.roundRect(c.x,c.y,c.w,c.h,c.r);if(c.outline)ctx.stroke();else ctx.fill();break;
      case 'circle':ctx.beginPath();ctx.arc(c.x,c.y,c.r,0,Math.PI*2);if(c.stroke){ctx.lineWidth=c.stroke;ctx.stroke();}else ctx.fill();break;
      case 'line':ctx.lineWidth=c.w;ctx.lineCap='round';ctx.beginPath();ctx.moveTo(c.x,c.y);ctx.lineTo(c.x2,c.y2);ctx.stroke();break;
      case 'text':report.push(drawText(ctx,typeof c.v==='function'?c.v(n):c.v,c.x,c.y,c.font,color,c.anchor,c.fixed));break;
      case 'bar':ctx.fillRect(c.x,c.y,Math.floor(c.w*(n.second+1)/60),c.h);break;
      case 'hands':{
        const hours=(n.hour%12)*30+n.minute*.5,mins=n.minute*6+n.second*.1,secs=n.second*6;
        for(const [angle,len,tail,w,stroke] of [[hours,c.r*.57,4,c.wide?7:5,c.fg],[mins,c.r*.82,6,3,c.fg],[secs,c.r,7,1.5,c.accent]]) {
          const end=point(c.x,c.y,len,angle),start=point(c.x,c.y,-tail,angle);
          ctx.strokeStyle=rgb565(stroke);ctx.lineWidth=w;ctx.lineCap='round';ctx.beginPath();ctx.moveTo(start.x,start.y);ctx.lineTo(end.x,end.y);ctx.stroke();
        }
        ctx.fillStyle=rgb565(c.accent);ctx.beginPath();ctx.arc(c.x,c.y,4,0,Math.PI*2);ctx.fill();break;
      }
    }
  }
  function quantize(ctx) {
    const image=ctx.getImageData(0,0,240,240),p=image.data;
    for(let i=0;i<p.length;i+=4) { const r=p[i]>>3,g=p[i+1]>>2,b=p[i+2]>>3;p[i]=(r<<3)|(r>>2);p[i+1]=(g<<2)|(g>>4);p[i+2]=(b<<3)|(b>>2);p[i+3]=255; }
    ctx.putImageData(image,0,0);
  }
  function render(canvas,themeId,face,n) {
    const theme=THEMES.find(t=>t.id===themeId)||THEMES[0],key=theme.id+'/'+face;
    let cached=backgroundCache.get(key);
    if(!cached) {
      const design=scene(theme,face),image=document.createElement('canvas');image.width=240;image.height=240;
      const staticTexts=[];for(const c of design.static)command(image.getContext('2d'),c,n,staticTexts);
      cached={design,image,staticTexts};backgroundCache.set(key,cached);
    }
    const ctx=canvas.getContext('2d',{willReadFrequently:true}),texts=[...cached.staticTexts];
    ctx.drawImage(cached.image,0,0);
    for(const c of cached.design.dynamic) command(ctx,c,n,texts);
    quantize(ctx);audit.renders++;
    return {texts,staticCount:cached.design.static.length,dynamicCount:cached.design.dynamic.length,theme:theme.id,face,time:{...n}};
  }
  class YumoClock extends HTMLElement {
    static get observedAttributes(){return ['theme','face'];}
    connectedCallback() {
      if(!this.canvas) {
        this.canvas=document.createElement('canvas');this.canvas.width=240;this.canvas.height=240;this.canvas.setAttribute('aria-hidden','true');this.append(this.canvas);
        this.setAttribute('tabindex','0');this.setAttribute('role','img');
        this.addEventListener('pointerdown',event=>{
          if(!event.isPrimary||event.button!==0)return;
          this.start={x:event.clientX,y:event.clientY,id:event.pointerId};this.setPointerCapture(event.pointerId);
        });
        this.addEventListener('pointerup',event=>{
          const s=this.start;this.start=null;if(!s||s.id!==event.pointerId)return;
          const scale=this.getBoundingClientRect().width/240,dx=(event.clientX-s.x)/scale,dy=(event.clientY-s.y)/scale;
          if(Math.abs(dx)>=30&&Math.abs(dx)>Math.abs(dy))this.cycle(dx<0?1:-1);
        });
        this.addEventListener('pointercancel',()=>{this.start=null;});
        this.addEventListener('keydown',event=>{
          if(event.key==='ArrowLeft'||event.key==='ArrowRight'){event.preventDefault();event.stopPropagation();this.cycle(event.key==='ArrowRight'?1:-1);}
        });
      }
      this.inView=false;this.last='';instances.add(this);observer.observe(this);this.draw(true);startTimer();
    }
    disconnectedCallback() { observer.unobserve(this);instances.delete(this);this.start=null;if(!instances.size)stopTimer(); }
    attributeChangedCallback(){if(this.canvas&&this.isConnected)this.draw(true);}
    get face(){return FACES.includes(this.getAttribute('face'))?this.getAttribute('face'):'digital';}
    cycle(delta){this.setAttribute('face',FACES[(FACES.indexOf(this.face)+delta+3)%3]);this.dispatchEvent(new CustomEvent('clock-face-change',{bubbles:true,detail:{face:this.face}}));}
    draw(force=false){
      if(!this.canvas)return;
      const n=civil(),stamp=JSON.stringify(n)+this.face+this.getAttribute('theme');
      if(!force&&(document.hidden||!this.inView||stamp===this.last))return;
      this.last=stamp;this.audit=render(this.canvas,this.getAttribute('theme'),this.face,n);
      this.setAttribute('aria-label',`${this.audit.theme} ${FACE_NAMES[FACES.indexOf(this.face)]}表盘 ${date(n)} ${hms(n)}，左右滑动切换`);
    }
  }
  const observer=new IntersectionObserver(entries=>{for(const entry of entries){entry.target.inView=entry.isIntersecting;if(entry.isIntersecting)entry.target.draw(true);}});
  function stopTimer(){if(timer!==null)clearInterval(timer);timer=null;}
  function startTimer(){if(timer!==null||document.hidden||!instances.size)return;timer=setInterval(()=>instances.forEach(c=>c.draw()),1000);}
  document.addEventListener('visibilitychange',()=>{if(document.hidden)stopTimer();else{instances.forEach(c=>c.draw());startTimer();}});
  window.addEventListener('pagehide',stopTimer);window.addEventListener('pageshow',startTimer);
  customElements.define('yumo-clock',YumoClock);
  window.ClockPreview={themes:THEMES,faces:FACES,names:FACE_NAMES,details:DETAILS,audit,render,width,
    setMode(value){if(!['demo','frozen','local'].includes(value))return;mode=value;started=Date.now();instances.forEach(c=>c.draw(true));},
    setTime(value){epoch=Date.UTC(value.year,value.month-1,value.day,value.hour,value.minute,value.second);started=Date.now();instances.forEach(c=>c.draw(true));},
    get mode(){return mode;},get stats(){return {mounted:instances.size,inView:[...instances].filter(c=>c.inView).length,timerRunning:timer!==null,backgrounds:backgroundCache.size,glyphCache:glyphCache.size,renders:audit.renders};}
  };
})();
