/* Browser-only device constraints. No hardware, service or filesystem APIs. */
'use strict';
(() => {
  const sizes=[8,10,12,14,16,18,20,22,24,26,32,40,48];
  const iconSizes=[12,16,18,20,24,32,40,48];
  const images=new Map();
  const audit={missingIcons:[],fonts:0};
  const nearest=(n,list)=>list.reduce((a,b)=>Math.abs(b-n)<Math.abs(a-n)?b:a);
  function color565(value){
    const m=value.match(/^rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\)$/);
    if(!m)return value;
    const r=Number(m[1])>>3,g=Number(m[2])>>2,b=Number(m[3])>>3;
    return `rgba(${(r<<3)|(r>>2)},${(g<<2)|(g>>4)},${(b<<3)|(b>>2)},${m[4]||1})`;
  }
  function paintIcon(node){
    const style=getComputedStyle(node),size=nearest(parseFloat(style.width)||24,iconSizes);
    const solid=node.dataset.symbol.endsWith(':record')||node.dataset.symbol.endsWith(':stop');
    const rotation=node.classList.contains('up-icon')?-90:node.classList.contains('down-icon')?90:0;
    const key=node.dataset.symbol+(solid?':solid':'')+'@'+size;
    const src=window.DeviceIconBitmaps?.[key];
    node.width=size;node.height=size;node.style.width=size+'px';node.style.height=size+'px';
    if(!src){if(!audit.missingIcons.includes(key))audit.missingIcons.push(key);return;}
    let bitmap=images.get(key);
    if(!bitmap){bitmap=new Image();bitmap.src=src;images.set(key,bitmap);}
    const draw=()=>{
      const ctx=node.getContext('2d');ctx.clearRect(0,0,size,size);ctx.save();
      ctx.translate(size/2,size/2);ctx.rotate(rotation*Math.PI/180);ctx.drawImage(bitmap,-size/2,-size/2);ctx.restore();
      ctx.globalCompositeOperation='source-in';ctx.fillStyle=color565(getComputedStyle(node).color);ctx.fillRect(0,0,size,size);ctx.globalCompositeOperation='source-over';
      node.dataset.ready='true';
    };
    if(bitmap.complete&&bitmap.naturalWidth)draw();else bitmap.addEventListener('load',draw,{once:true});
  }
  function adapt(root){
    for(const el of [root,...root.querySelectorAll('*')]){
      if(el.closest('yumo-clock')||el.tagName==='CANVAS')continue;
      const style=getComputedStyle(el);
      const text=[...el.childNodes].filter(n=>n.nodeType===Node.TEXT_NODE).map(n=>n.textContent).join('')+(el.getAttribute('placeholder')||'');
      if(text.trim()){
        const cjk=/[\u3400-\u9fff]/.test(text);
        const menu=el.classList.contains('app-name');
        const size=cjk?(menu?14:16):nearest(parseFloat(style.fontSize),sizes);
        el.style.fontSize=size+'px';el.style.fontWeight='400';el.style.letterSpacing='0';
        if(cjk)el.style.lineHeight=(menu?18:21)+'px';
        el.dataset.deviceFont=cjk?(menu?'menu14':'cjk16'):'montserrat'+size;
        audit.fonts++;
      }
      for(const property of ['color','backgroundColor','borderTopColor','borderRightColor','borderBottomColor','borderLeftColor']){
        const value=color565(style[property]);if(value!==style[property])el.style[property]=value;
      }
    }
    root.querySelectorAll('canvas[data-symbol]').forEach(paintIcon);
  }
  window.DevicePreview={adapt,audit,icon:(set,name,cls='')=>`<canvas class="icon ${cls}" data-symbol="${set}:${name}" aria-hidden="true"></canvas>`};
})();
