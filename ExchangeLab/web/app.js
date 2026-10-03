'use strict';
const $ = id => document.getElementById(id);
const fmt = n => n == null ? '—' : Number(n).toLocaleString('en-US');
let token = '', side = 'BUY', state = null, busy = false, playing = false, timer = null;
let previousTrade = 0;
function node(tag, className, text) { const n = document.createElement(tag); if(className) n.className=className; if(text!=null)n.textContent=text;return n; }
function toast(message) { $('toast').textContent=message;$('toast').classList.add('visible');clearTimeout(timer);timer=setTimeout(()=>$('toast').classList.remove('visible'),3500); }
function stop(){playing=false;$('play').textContent='Play';}
async function action(payload) {
  if(busy) return null;
  busy=true;
  document.querySelectorAll('button').forEach(b=>b.disabled=true);
  try {
    const response=await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/json','X-Session-Token':token},body:JSON.stringify(payload)});
    const data=await response.json();
    if(!response.ok)throw new Error(data.error || 'Request failed');
    render(data);
    if(!data.ok)toast(data.message || data.status);
    return data;
  } catch(error){stop();toast(error.message);return null;}
  finally {busy=false;document.querySelectorAll('button').forEach(b=>b.disabled=false);}
}
function renderBook(id, levels, isBuy) {
  const root=$(id);root.replaceChildren();
  if(!levels.length){root.append(node('div','empty','No resting orders'));return;}
  const max=Math.max(...levels.slice(0,8).map(d=>d.quantity));
  for(const level of levels.slice(0,8)) {
    const row=node('div','row '+(isBuy?'bid-row':'ask-row'));
    const bar=node('i','bar');bar.style.width=(level.quantity/max*100)+'%';row.append(bar);
    const values=isBuy?[level.orders,level.quantity,level.price]:[level.price,level.quantity,level.orders];
    values.forEach((value,i)=>row.append(node('span',i===(isBuy?2:0)?(isBuy?'buy':'sell'):'',fmt(value))));
    root.append(row);
  }
}
function svgNode(tag,attrs,text){const n=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [k,v] of Object.entries(attrs))n.setAttribute(k,v);if(text!=null)n.textContent=text;return n;}
function chart(book){
  const svg=$('depth-chart');svg.replaceChildren();
  const all=[...book.bids,...book.asks];
  if(!all.length){svg.append(svgNode('text',{x:300,y:85,'text-anchor':'middle',fill:'#777b73','font-size':12},'Load liquidity to inspect depth'));return;}
  let lo=Math.min(...all.map(d=>d.price)),hi=Math.max(...all.map(d=>d.price));if(lo===hi){lo-=1;hi+=1;}
  const totals=[book.bids,book.asks].map(levels=>levels.reduce((n,l)=>n+l.quantity,0));
  const ymax=Math.max(...totals,1);const x=p=>45+(p-lo)/(hi-lo)*520;const y=q=>145-q/ymax*115;
  for(let i=0;i<=2;i++){let q=ymax*i/2;svg.append(svgNode('line',{x1:45,y1:y(q),x2:565,y2:y(q),stroke:'#dde1d5','stroke-width':1}));svg.append(svgNode('text',{x:38,y:y(q)+3,'text-anchor':'end',fill:'#777b73','font-size':9},fmt(Math.round(q))));}
  [book.bids,book.asks].forEach((levels,i)=>{
    if(!levels.length)return;let q=0;let path=`M ${x(levels[0].price)} 145`;
    for(const l of levels){path+=` L ${x(l.price)} ${y(q)}`;q+=l.quantity;path+=` L ${x(l.price)} ${y(q)}`;}
    const area=path+` L ${x(levels[levels.length-1].price)} 145 Z`;
    svg.append(svgNode('path',{d:area,fill:i?'#f1e4dd':'#e0ebe1'}));
    svg.append(svgNode('path',{d:path,fill:'none',stroke:i?'#a44f3c':'#256850','stroke-width':1.8}));
  });
  for(let i=0;i<3;i++){const p=lo+(hi-lo)*i/2;svg.append(svgNode('text',{x:x(p),y:166,'text-anchor':'middle',fill:'#777b73','font-size':9},fmt(Math.round(p))));}
}
function render(data){
  state=data;const b=data.book;
  $('connection').textContent='ENGINE ONLINE';$('connection').classList.add('online');
  $('best-bid').textContent=fmt(b.bids[0]?.price);$('best-ask').textContent=fmt(b.asks[0]?.price);
  $('spread').textContent=b.bids.length&&b.asks.length?fmt(b.asks[0].price-b.bids[0].price):'—';
  $('volume').textContent=fmt(b.volume);$('trade-count').textContent=fmt(b.trade_count)+' trades this session';
  $('active-count').textContent=fmt(b.active_count)+' ACTIVE ORDERS';$('event-count').textContent=fmt(data.event_count)+' EVENTS';
  renderBook('bids',b.bids,true);renderBook('asks',b.asks,false);chart(b);
  const tape=$('trades');tape.replaceChildren();tape.classList.toggle('empty',!b.trades.length);
  if(!b.trades.length)tape.textContent='No executions yet.';
  for(const t of [...b.trades].reverse()){
    const row=node('div','trade-row'+(t.sequence>previousTrade?' fresh':''));
    const left=node('div');left.append(node('strong','',fmt(t.price)),node('small','',` / ${fmt(t.quantity)} units`));
    const right=node('div','ids');right.append(node('div','',`${t.maker} → ${t.taker}`),node('small','',`#${t.sequence} · B${t.buy} / S${t.sell}`));
    row.append(left,right);tape.append(row);
  }previousTrade=b.trade_count;
  const events=$('events');events.replaceChildren();events.classList.toggle('empty',!data.events.length);
  if(!data.events.length)events.textContent='Load liquidity or submit an order.';
  for(const e of [...data.events].reverse()){
    const row=node('div','event');row.append(node('span','',String(e.number).padStart(3,'0')),node('strong','event-command',e.command),node('span',e.status==='REJECTED'?'rejected':'',e.status));
    if(e.message)row.title=e.message;events.append(row);
  }
  const working=$('working');working.replaceChildren();
  for(const o of b.orders){
    const row=node('tr');[o.id,o.side,fmt(o.price),fmt(o.remaining),o.sequence].forEach((v,i)=>row.append(node('td',i===1?o.side.toLowerCase():'',v)));
    const td=node('td'),button=node('button','','Cancel');button.setAttribute('aria-label',`Cancel order ${o.id}`);
    button.onclick=()=>{stop();action({action:'command',command:`CANCEL ${o.id}`});};td.append(button);row.append(td);working.append(row);
  }
  if(!b.orders.length){const row=node('tr'),cell=node('td','empty','No working orders.');cell.colSpan=6;row.append(cell);working.append(row);}
  if(data.status!=='SNAPSHOT' && data.status!=='RESET'){
    const r=$('receipt');r.replaceChildren(node('span','eyebrow','EXECUTION RECEIPT'),node('strong',data.ok?'':'sell',`${data.id ? '#'+data.id+' / ':''}${data.status}`));
    r.append(node('div','receipt-numbers',`Filled ${fmt(data.filled)} · Resting ${fmt(data.resting)} · Cancelled ${fmt(data.cancelled)}`));
    if(data.message)r.append(node('p','',data.message));
  }else if(data.status==='RESET'){$('receipt').replaceChildren(node('span','eyebrow','EXECUTION RECEIPT'),node('p','','Fresh session. No orders submitted.'));}
  $('replay-position').textContent=`${data.replay_cursor} / ${data.replay_count}`;
  $('replay-progress').max=Math.max(data.replay_count,1);$('replay-progress').value=data.replay_cursor;
  $('replay-last').textContent=data.events.length?data.events[data.events.length-1].command:'No events processed.';
  $('replay-summary').textContent=`${b.active_count} active orders · ${b.trade_count} trades · ${fmt(b.volume)} units executed`;
  if(data.replay_cursor>=data.replay_count)stop();
  if(data.benchmark)renderBenchmark(data.benchmark);
}
function renderBenchmark(b){
  const root=$('benchmark-results');root.classList.remove('empty');root.replaceChildren();
  const table=node('table'),head=node('thead'),hr=node('tr');
  ['ENGINE','EVENTS / SEC','p50 / µs','p95 / µs','p99 / µs'].forEach(s=>hr.append(node('th','',s)));head.append(hr);table.append(head);
  const body=node('tbody');for(const r of b.results){const row=node('tr');[r.engine,fmt(Math.round(r.events_per_second)),(r.p50_ns/1000).toFixed(2),(r.p95_ns/1000).toFixed(2),(r.p99_ns/1000).toFixed(2)].forEach(s=>row.append(node('td','',s)));body.append(row);}table.append(body);root.append(table);
  root.append(node('p','benchmark-meta',`${b.system} / ${b.machine} · ${b.cpu_count} logical CPUs · ${b.compiler} · ${b.build_flags} · ${b.results[0].events_per_run} events × ${b.results[0].runs} runs`));
  const button=node('button','','Download results JSON');button.onclick=()=>download('exchangelab-benchmark.json',JSON.stringify(b,null,2),'application/json');root.append(button);
}
function download(filename,text,type){const url=URL.createObjectURL(new Blob([text],{type}));const a=node('a');a.href=url;a.download=filename;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
document.querySelectorAll('[data-tab]').forEach(button=>button.onclick=()=>{document.querySelectorAll('[data-tab]').forEach(x=>x.classList.toggle('selected',x===button));document.querySelectorAll('.tab-panel').forEach(x=>x.classList.toggle('hidden',x.id!==button.dataset.tab));});
document.querySelectorAll('[data-side]').forEach(button=>button.onclick=()=>{side=button.dataset.side;document.querySelectorAll('[data-side]').forEach(x=>x.classList.toggle('active',x===button));$('submit-order').replaceChildren(document.createTextNode(`Submit ${side.toLowerCase()} order `),node('span','','→'));});
$('order-type').onchange=()=>{const type=$('order-type').value;$('price').disabled=type==='MARKET';$('type-help').textContent={GTC:'Unfilled quantity joins the book at your limit price.',IOC:'Execute eligible quantity now; cancel the remainder.',FOK:'Execute the entire quantity now, or execute nothing.',MARKET:'Consume available opposite liquidity. Unfilled remainder cancels.'}[type];};
$('order-form').onsubmit=async e=>{e.preventDefault();stop();const type=$('order-type').value,id=$('order-id').value;const result=await action({action:'command',command:`${side} ${id} ${type==='MARKET'?0:$('price').value} ${$('quantity').value} ${type}`});if(result?.ok && Number(id)<9007199254740991)$('order-id').value=String(Number(id)+1);};
$('seed').onclick=()=>{stop();action({action:'seed'});};
$('reset').onclick=()=>{stop();if(confirm('Clear orders, trades and the current event journal? Export first if you need it.'))action({action:'reset'});};
document.querySelectorAll('[data-scenario]').forEach(b=>b.onclick=()=>{stop();action({action:'scenario',scenario:b.dataset.scenario});});
$('load-replay').onclick=()=>{stop();action({action:'load_replay',text:$('replay-text').value});};
$('step').onclick=()=>{stop();action({action:'step'});};
$('pause').onclick=stop;
$('play').onclick=async()=>{
  if(!state?.replay_count){toast('Load a replay first.');return;}
  if(state.replay_cursor>=state.replay_count){toast('Replay complete. Load it again to restart.');return;}
  if(playing)return;playing=true;$('play').textContent='Playing…';
  while(playing && state.replay_cursor<state.replay_count){const r=await action({action:'step'});if(!r)break;await new Promise(resolve=>setTimeout(resolve,500));}
  stop();
};
$('import-file').onchange=async e=>{const file=e.target.files[0];if(!file)return;if(file.size>250000){toast('Choose a text file smaller than 250KB.');return;}$('replay-text').value=await file.text();toast('Imported. Press Load replay & reset to begin.');};
$('export').onclick=async()=>{try{const r=await fetch('/api/export');if(!r.ok)throw new Error('Export failed');download('exchangelab-events.txt',await r.text(),'text/plain');}catch(e){toast(e.message);}};
$('benchmark').onclick=async()=>{stop();$('benchmark').textContent='Measuring…';try{await action({action:'benchmark'});}finally{$('benchmark').textContent='Run local benchmark →';}};
(async()=>{try{const r=await fetch('/api/state');if(!r.ok)throw new Error('Cannot connect to local engine');const data=await r.json();token=data.token;render(data);}catch(e){$('connection').textContent='OFFLINE';toast(e.message);}})();
