/* Offline, bounded random-app simulator. No filesystem, radio or service API. */
'use strict';
(() => {
  const modes=['数字','骰子','选项英文','选项数字','双色球','硬币','胜负','石头剪刀布'];
  const hands=['石头','剪刀','布'],pips=[0x010,0x101,0x111,0x145,0x155,0x16d],instances=new Set();
  const family=id=>({clear:'warm',sage:'warm',night:'tiles',orbit:'warm',rail:'mono',tabs:'warm',folio:'mono',matrix:'tiles',ribbon:'focus',split:'tiles'}[id]||id);
  const palettes={warm:['#f6f0e7','#fffcf6','#594737','#b95435','#75604d','#d2bfa8','#ffffff'],mono:['#f1f2ee','#f9faf6','#24362d','#344f3d','#56624f','#b9c7b3','#ffffff'],tiles:['#0e1b26','#203443','#f2f7fb','#c6f26d','#b0c1ce','#3e5260','#0e1b26'],focus:['#f1efeb','#e6e2d9','#282723','#b63b17','#665e51','#c6c1b6','#ffffff'],terminal:['#101e1b','#203b2b','#b8d6bd','#d5ef8d','#a0b7a2','#405b42','#101e1b']};
  function numberLayout(id){
    if(id==='tiles')return [[0,0,145,136],[0,139,240,22],[149,0,91,22],[149,77,91,22],[149,23,91,36],[149,100,91,36]];
    if(id==='warm'||id==='focus')return [[4,0,232,80],[0,81,240,22],[2,104,117,22],[121,104,117,22],[2,126,117,36],[121,126,117,36]];
    return [[0,60,240,77],[0,138,240,22],[0,0,119,22],[120,0,120,22],[0,23,119,36],[120,23,120,36]];
  }
  function cellLayout(id,count,index,height){
    const inset=id==='warm'?3:0,gap=id==='warm'?4:id==='focus'?3:id==='tiles'?4:1;
    const cols=count<=3?count:count===4?2:count<=9?(id==='focus'&&count<=8&&count!==5?2:3):4;
    const rows=Math.ceil(count/cols),row=Math.floor(index/cols),col=index%cols,n=Math.min(cols,count-row*cols);
    const w=240-2*inset-(n-1)*gap,h=height-2*inset-(rows-1)*gap;
    return [inset+Math.floor(w*col/n)+col*gap,inset+Math.floor(h*row/rows)+row*gap,Math.floor(w*(col+1)/n)-Math.floor(w*col/n),Math.floor(h*(row+1)/rows)-Math.floor(h*row/rows)];
  }
  function ballLayout(id,i){
    if(id==='focus')return i===6?[90,119,60,40]:[(i%3)*81,25+Math.floor(i/3)*46,78,42];
    if(id==='tiles')return i===6?[176,25,64,133]:[(i%3)*58,25+Math.floor(i/3)*68,54,64];
    return i===6?[178,62,60,60]:[(i%3)*55,29+Math.floor(i/3)*60,52,52];
  }
  function below(n){const data=new Uint32Array(1),threshold=(0x100000000-n)%n;do{crypto.getRandomValues(data);}while(data[0]<threshold);return data[0]%n;}
  const integer=(a,b)=>a+below(b-a+1);
  function place(el,r){['left','top','width','height'].forEach((key,i)=>el.style[key]=r[i]+'px');return el;}
  function node(parent,tag,cls,text='',r=null){const el=document.createElement(tag);el.className=cls;el.textContent=text;if(r)place(el,r);parent.append(el);return el;}
  function mount(screen,theme,options={}){
    const id=family(theme.id),s={scene:'random',mode:0,letters:4,choices:4,low:'1',high:'100',running:false,pk:false,score:[0,0],result:-1,cursors:[-1,-1],players:[0,0],step:0};
    const colors=palettes[theme.id]||[theme.bg,theme.surface,theme.fg,theme.accent,theme.muted,theme.edge,['matrix','orbit','ribbon','split','night'].includes(theme.id)?theme.bg:'#fff'];
    ['bg','surface','fg','accent','muted','edge','on'].forEach((k,i)=>screen.style.setProperty('--'+k,colors[i]));
    screen.style.setProperty('--radius',(id==='warm'?12:id==='tiles'?9:id==='focus'?2:0)+'px');screen.style.setProperty('--outline',id==='tiles'?'0px':'1px');
    screen.className='random-screen';screen.dataset.theme=theme.id;screen.dataset.family=id;
    let visible=false,disposed=false,timer=null,body,select,start,pkButton,cells=[],number,hint,counts,fields=[],scores=[],pkCells=[],resultLabel;
    const stopTimer=()=>{if(timer!==null)clearTimeout(timer);timer=null;};
    const count=()=>s.mode===1?6:s.mode===2?s.letters:s.mode===3?s.choices:s.mode===5?2:s.mode===6?5:3;
    function lock(){select.disabled=s.running;fields.forEach(el=>el.disabled=s.running);if(counts)counts.disabled=s.running;pkButton.disabled=s.running;start.disabled=s.running&&!s.pk;}
    function paint(){
      if(s.pk){
        for(let side=0;side<2;side++){
          scores[side].textContent=(side?'红 ':'蓝 ')+s.score[side];
          pkCells[side].forEach((cell,i)=>{cell.classList.toggle('current',s.cursors[side]===i);cell.classList.toggle('final',s.cursors[side]===i&&s.players[side]===2);});
        }
        resultLabel.textContent=['蓝胜','红胜','平局'][s.result]||'VS';
      }else if(s.mode!==0&&s.mode!==4)cells.forEach((cell,i)=>{cell.classList.toggle('current',i===s.cursor);cell.classList.toggle('final',i===s.cursor&&!s.running);});
      lock();
    }
    function cancel(){stopTimer();s.running=false;s.players=[0,0];s.cursors=[-1,-1];s.cursor=-1;s.result=-1;if(start)start.textContent='开始';if(number)number.textContent='--';if(hint)hint.textContent='范围 -999999～999999';cells.forEach(c=>{c.classList.remove('current','final','revealed');if(s.mode===4)c.textContent='--';});if(select)paint();}
    function render(){
      stopTimer();screen.replaceChildren();fields=[];cells=[];counts=number=hint=null;s.cursor=-1;s.running=false;
      const header=node(screen,'div','random-header');const back=node(header,'button','random-back','‹');back.setAttribute('aria-label','返回应用列表');back.onclick=()=>options.onRoute?.('home');
      select=node(header,'select','random-mode');select.setAttribute('aria-label','随机模式');modes.forEach((text,i)=>{const o=node(select,'option','',text);o.value=i;});select.value=s.mode;
      select.onchange=()=>{if(s.running)return;s.mode=Number(select.value);s.pk=false;s.result=-1;s.players=[0,0];s.cursors=[-1,-1];render();};
      body=node(screen,'div','random-body');start=node(screen,'button','random-start','开始');start.onclick=()=>begin(3);
      pkButton=node(screen,'button','random-pk'+(s.pk?' active':''),'PK');pkButton.hidden=s.mode!==7;pkButton.setAttribute('aria-pressed',String(s.pk));
      pkButton.onclick=()=>{if(s.running)return;s.pk=!s.pk;s.result=-1;s.players=[0,0];s.cursors=[-1,-1];render();};if(s.mode===7)start.style.width='179px';
      if(s.pk){
        scores=[];pkCells=[];const light=['warm','mono','focus'].includes(id);
        for(let side=0;side<2;side++){
          scores[side]=node(body,'div','pk-score','',[side?140:0,0,100,22]);scores[side].style.color=side?(light?'#a92733':'#f1707a'):(light?'#18558d':'#69b4ff');
          pkCells[side]=hands.map((hand,i)=>node(body,'div','random-cell pk-cell '+(side?'red':'blue'),hand,[side?162:0,26+i*46,78,44]));
        }
        node(body,'div','pk-colon',':');resultLabel=node(body,'div','pk-result','VS');paint();
      }else if(s.mode===0){
        const g=numberLayout(id);number=node(body,'div','random-result','--',g[0]);hint=node(body,'div','random-hint','范围 -999999～999999',g[1]);
        ['最小值','最大值'].forEach((text,i)=>{node(body,'div','random-label',text,g[2+i]);const el=node(body,'input','random-param','',g[4+i]);el.type='text';el.inputMode='text';el.maxLength=7;el.value=i?s.high:s.low;el.setAttribute('aria-label',text);el.oninput=()=>s[i?'high':'low']=el.value;fields.push(el);});
      }else if(s.mode===4){
        node(body,'div','random-label','红球',[0,0,id==='focus'?240:164,22]);node(body,'div','random-label','蓝球',[id==='focus'?8:166,id==='focus'?128:0,74,22]);
        cells=Array.from({length:7},(_,i)=>node(body,'div','random-cell random-ball'+(i===6?' blue':''),'--',ballLayout(id,i)));
      }else{
        const adjustable=s.mode===2||s.mode===3,top=adjustable?37:0,height=162-top;
        if(adjustable){node(body,'div','random-label',s.mode===2?'A 到':'选项数量',[0,7,102,22]);counts=node(body,'select','random-count');counts.setAttribute('aria-label','选项数量');for(let n=2;n<=(s.mode===2?8:16);n++){const o=node(counts,'option','',s.mode===2?String.fromCharCode(64+n):String(n));o.value=n;}counts.value=count();counts.onchange=()=>{s[s.mode===2?'letters':'choices']=Number(counts.value);render();};}
        const board=node(body,'div','random-board','',[0,top,240,height]);
        cells=Array.from({length:count()},(_,i)=>{
          const r=cellLayout(id,count(),i,height);let label=s.mode===2?String.fromCharCode(65+i):String(i+1);
          if(s.mode===5)label=['正','反'][i];if(s.mode===6)label=['胜','平','负','让平','让负'][i];if(s.mode===7)label=hands[i];
          const cell=node(board,'div','random-cell'+(s.mode>=5?' word':''),s.mode===1?'':label,r);
          if(s.mode<5&&r[3]<32)cell.style.fontSize='18px';
          if(s.mode===1){const size=Math.min(r[2],r[3]),step=Math.floor(size/4),dot=Math.max(6,Math.floor(size/9));for(let p=0;p<9;p++)if(pips[i]&(1<<p))node(cell,'i','dice-pip','',[Math.floor(r[2]/2)+(p%3-1)*step-Math.floor(dot/2),Math.floor(r[3]/2)+(Math.floor(p/3)-1)*step-Math.floor(dot/2),dot,dot]);cell.setAttribute('aria-label',(i+1)+'点');}
          return cell;
        });
      }
      options.onScene?.('random');lock();
    }
    function showNumber(value,final){
      number.textContent=value;number.classList.toggle('final',final);const w=numberLayout(id)[0][2]-8;
      // Same supported font-size ladder as firmware; fit the whole signed range.
      const canvas=document.createElement('canvas'),ctx=canvas.getContext('2d');
      for(const size of [48,40,32,24]){ctx.font=size+'px Arial';if(ctx.measureText(String(value)).width<=w){number.style.fontSize=size+'px';break;}}
    }
    function schedule(delay){stopTimer();if(!disposed&&visible&&!document.hidden&&s.running)timer=setTimeout(tick,delay);}
    function begin(mask=3){
      if(disposed||!visible||document.hidden)return;
      if(s.pk){
        if(s.result>=0){s.result=-1;s.players=[0,0];s.cursors=[-1,-1];}
        let changed=false;
        for(let side=0;side<2;side++)if((mask&(1<<side))&&s.players[side]===0){s.players[side]=1;s.pkStep[side]=0;s.pkTotal[side]=13+below(3);s.pkDue[side]=performance.now();changed=true;}
        if(!changed)return;s.running=true;start.textContent='开始';paint();schedule(1);return;
      }
      if(s.running)return;
      if(s.mode===0){
        const valid=x=>/^-?\d{1,6}$/.test(x);const low=Number(s.low),high=Number(s.high);
        hint.classList.remove('error');if(!valid(s.low)||!valid(s.high)||low>high){hint.textContent=low>high?'最小值不能大于最大值':'请输入有效整数';hint.classList.add('error');return;}
        s.lowValue=low;s.highValue=high;s.value=integer(low,high);s.total=24;hint.textContent='抽取中';number.classList.remove('final');
      }else if(s.mode===4){const pool=Array.from({length:33},(_,i)=>i+1),red=[];for(let i=0;i<6;i++)red.push(pool.splice(below(pool.length),1)[0]);s.balls=red.sort((a,b)=>a-b).concat(1+below(16));s.total=7;cells.forEach(c=>{c.textContent='--';c.classList.remove('revealed');});}
      else{s.total=count()*3+below(count())+1;s.cursor=-1;}
      s.step=0;s.running=true;start.textContent='抽取中';paint();schedule(1);
    }
    function tick(){
      timer=null;if(disposed||!visible||document.hidden){cancel();return;}
      if(s.pk){
        const now=performance.now();for(let side=0;side<2;side++)if(s.players[side]===1&&now>=s.pkDue[side]){s.cursors[side]=s.pkStep[side]++%3;const left=s.pkTotal[side]-s.pkStep[side];if(!left)s.players[side]=2;else s.pkDue[side]=now+(left<7?65+(7-left)*22:45);}
        if(s.result<0&&s.players.every(p=>p===2)){const a=s.cursors[0],b=s.cursors[1];s.result=a===b?2:(a+1)%3===b?0:1;if(s.result<2)s.score[s.result]=Math.min(9999,s.score[s.result]+1);}
        s.running=s.players.includes(1);if(!s.running)start.textContent=s.result<0?'开始':'再来一次';paint();if(s.running)schedule(20);return;
      }
      const final=++s.step>=s.total;
      if(s.mode===0){showNumber(final?s.value:integer(s.lowValue,s.highValue),final);if(final)hint.textContent='结果';}
      else if(s.mode===4){cells[s.step-1].textContent=String(s.balls[s.step-1]).padStart(2,'0');cells[s.step-1].classList.add('revealed');}
      else s.cursor=(s.step-1)%count();
      if(final){s.running=false;start.textContent='再来一次';}paint();
      if(!final)schedule(s.mode===4?480:s.mode===0?45+(s.step>14?(s.step-14)*20:0):s.total-s.step<8?60+(8-(s.total-s.step))*22:45);
    }
    s.pkStep=[0,0];s.pkTotal=[0,0];s.pkDue=[0,0];
    const key=side=>begin(s.pk?(side==='boot'?1:2):3);
    const keydown=event=>{if(/INPUT|SELECT|BUTTON/.test(event.target.tagName)||event.repeat)return;const k=event.key.toLowerCase();if(['b','k','enter',' '].includes(k)){event.preventDefault();k==='b'?key('boot'):k==='k'?key('key'):begin(3);}};
    const visibility=()=>{if(document.hidden)cancel();};screen.addEventListener('keydown',keydown);document.addEventListener('visibilitychange',visibility);
    const controller={state:s,navigate(target){if(target==='random')render();else options.onRoute?.(target);},key,setVisible(value){visible=value;if(!visible&&s.running)cancel();},dispose(){if(disposed)return;cancel();disposed=true;screen.removeEventListener('keydown',keydown);document.removeEventListener('visibilitychange',visibility);instances.delete(controller);},get timerRunning(){return timer!==null;}};
    instances.add(controller);return controller;
  }
  window.RandomPreview={mount,modes,geometry:{numberLayout,cellLayout,ballLayout},get stats(){return {mounted:instances.size,timers:[...instances].filter(c=>c.timerRunning).length};}};
})();
