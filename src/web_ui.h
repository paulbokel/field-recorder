#pragma once
// Companion web interface, mirrors "Field Recorder Web" (design). Served at http://192.168.4.1 / http://field-xxxx.local
static const char WEB_UI[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Field Recorder</title>
<link rel="icon" type="image/svg+xml" href="/favicon.svg"><link rel="icon" type="image/png" sizes="32x32" href="/favicon-32.png"><link rel="apple-touch-icon" href="/apple-touch-icon.png"><meta name="theme-color" content="#141414">
<style>
:root{--ink:#141414;--paper:#F3F1EA;--hov:#E8E5DE;--mono:'Geist Mono',ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;--serif:Literata,Charter,Georgia,serif}
*{box-sizing:border-box}html,body{margin:0;background:var(--paper);color:var(--ink);font-family:var(--mono)}
a{color:var(--ink)}a:hover{color:#55534e}
button,input,select{font-family:var(--mono);color:var(--ink)}
input:focus,select:focus,button:focus-visible{outline:2px solid var(--ink);outline-offset:2px}
.top{min-height:64px;display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px 24px;padding:12px 32px;border-bottom:2px solid var(--ink)}
.brand{display:flex;align-items:baseline;gap:14px;white-space:nowrap}.sf{font-family:var(--serif)}
.c11{font-size:11px;letter-spacing:.12em}.c12{font-size:12px;letter-spacing:.12em}.c13{font-size:13px;letter-spacing:.1em}
.stat{display:flex;flex-wrap:wrap;gap:6px 20px;white-space:nowrap}
.body{flex:1;display:flex;flex-wrap:wrap;align-items:stretch;min-height:calc(100vh - 66px)}
.side{flex:0 0 240px;padding:28px 16px 28px 18px;border-right:2px solid var(--ink);display:flex;flex-direction:column;gap:4px}
.nav{display:flex;align-items:center;gap:12px;height:46px;padding:0;background:none;border:0;cursor:pointer;text-align:left;width:100%}
.nav.s{height:40px}.nav .l{flex:1;font-family:var(--serif);font-size:23px;white-space:nowrap}.nav.s .l{font-size:19px}
.mk{width:22px;flex:none;display:flex;justify-content:center}.mk i{width:12px;height:12px;background:var(--ink);display:block}
main{flex:1 1 560px;min-width:0;padding:28px 40px 64px}
.crumbs{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;font-size:11px;letter-spacing:.14em;margin-bottom:24px;min-height:16px}
.crumbs button{padding:0;background:none;border:0;font-size:11px;letter-spacing:.14em;cursor:pointer;text-decoration:underline;text-underline-offset:3px}
.crumbs span.p{white-space:nowrap;max-width:36ch;overflow:hidden;text-overflow:ellipsis}
.h1{font-family:var(--serif);font-size:44px;letter-spacing:-.02em;font-weight:400;margin:0}
.btn{height:40px;padding:0 16px;background:transparent;border:2px solid var(--ink);font-size:12px;letter-spacing:.14em;cursor:pointer;display:inline-flex;align-items:center;text-decoration:none}
.btn:hover{background:var(--hov);color:var(--ink)}.btn.k{background:var(--ink);color:var(--paper)}.btn.k:hover{background:#33322f;color:var(--paper)}
.btn.sm{height:32px;padding:0 12px;font-size:11px;letter-spacing:.12em}.btn.lg{height:48px;padding:0 22px;font-size:13px}
.btn[disabled]{opacity:.25;cursor:default}
.sec{font-size:11px;letter-spacing:.14em;margin-top:36px;padding-bottom:8px;border-bottom:2px solid var(--ink)}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:10px 16px;padding:16px 0;border-bottom:1px solid var(--ink)}
.rt{font-family:var(--serif);font-size:22px}.rs{font-size:11px;letter-spacing:.12em;margin-top:4px}
.in{height:42px;padding:0 10px;border:2px solid var(--ink);background:transparent;font-size:15px;min-width:0}
.tag{font-size:10px;letter-spacing:.14em;padding:3px 6px;background:var(--ink);color:var(--paper);white-space:nowrap}
.tag.l{background:none;color:var(--ink);border:2px solid var(--ink);padding:1px 4px}.tag.d{background:none;color:var(--ink);border:2px dashed var(--ink);padding:1px 4px}
.bar{height:18px;border:2px solid var(--ink);padding:2px;display:flex}.bar i{background:var(--ink);display:block}
.sig{display:flex;align-items:flex-end;gap:3px;height:18px}.sig i{width:5px;border:2px solid var(--ink);display:block}.sig i.on{background:var(--ink)}
.list button.it{width:100%;display:flex;align-items:center;gap:12px;padding:14px 0;background:none;border:0;border-bottom:1px solid var(--ink);cursor:pointer;text-align:left}
.list button.it:hover{background:var(--hov)}
.step{display:flex;align-items:center;gap:14px;padding:14px 0;border-bottom:1px solid var(--ink)}
.dot{width:12px;height:12px;display:block}.dot.done{background:var(--ink)}.dot.act{border:2px solid var(--ink)}.dot.pend{width:4px;height:4px;background:var(--ink)}
.toast{position:fixed;left:0;right:0;bottom:24px;display:flex;justify-content:center;pointer-events:none}
.toast span{background:var(--ink);color:var(--paper);font-size:12px;letter-spacing:.14em;padding:10px 16px}
@media(max-width:820px){.side{flex:1 1 100%;border-right:0;border-bottom:2px solid var(--ink);padding:16px}main{padding:24px 20px 56px}.top{padding:12px 20px}}
</style></head><body>
<div class="top"><div class="brand"><span class="sf" style="font-size:24px;letter-spacing:-.01em">Field Recorder</span><span class="c12">WEB INTERFACE</span></div><div class="stat c12" id="stat"></div></div>
<div id="app"></div>
<audio id="au" preload="none"></audio>
<div class="toast" id="toast"></div>
<script>
const $=s=>document.querySelector(s),au=$('#au');
let S=null,err=false,view=location.hash.slice(1)||'recordings',selId=null,tab='transcript',confirmDel=false,playOn='browser',det=null,detKey='',joinFor=null,showPw=false,hsDraft=null,hsShow=false,hsConfirm=false,showKey=false,phoneSent=false,T={},RT=null,rtOpen=null,rtDraft=null,rtConfirm=null,repSel=null,repFor=null,repConfirm=null,fwUp=null;
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const p2=n=>String(n).padStart(2,'0');
const fmt=s=>{s=Math.max(0,Math.floor(s||0));const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return h?h+':'+p2(m)+':'+p2(x):p2(m)+':'+p2(x)};
const MON=['JAN','FEB','MAR','APR','MAY','JUN','JUL','AUG','SEP','OCT','NOV','DEC'];
const dstr=ts=>{if(!ts)return 'NO DATE';const d=new Date(ts*1000);return d.getDate()+' '+MON[d.getMonth()]+' · '+p2(d.getHours())+':'+p2(d.getMinutes())};
const mb=d=>(d*32000/1e6).toFixed(1)+' MB';
const LANGS=['Auto-detect','English','Nederlands','Deutsch','Français'],MODELS=['large-v3','medium','small'];
const dots=()=>'.'.repeat(1+Math.floor(Date.now()/800)%3);
async function api(p,b){const r=await fetch(p,b===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||'ERROR');return j}
async function act(p,b,msg){try{await api(p,b);if(msg)toast(msg)}catch(e){toast(e.message)}poll()}
function toast(m){$('#toast').innerHTML='<span>'+esc(m)+'</span>';clearTimeout(T.t);T.t=setTimeout(()=>$('#toast').innerHTML='',2600)}
async function poll(){clearTimeout(T.p);try{S=await api('/api/state'+(document.visibilityState==='visible'?'?v=1':''));err=false}catch(e){err=true}render();
  const fast=S&&((S.ota&&(S.ota.checking||S.ota.installing))||S.rec.active||S.send.running||S.prog.running||S.wifi.scanning||S.play.active||S.phone.active||S.rep.running);T.p=setTimeout(poll,err?4000:fast?1000:3000)}
const rec=()=>S&&S.recs.find(r=>r.id===selId);
function go(v){view=v;joinFor=null;hsConfirm=false;location.hash=v;render(true)}
const sig=n=>'<span class="sig">'+[5,9,13,17].map((h,j)=>'<i class="'+(j<n?'on':'')+'" style="height:'+h+'px"></i>').join('')+'</span>';
const mk=on=>'<span class="mk">'+(on?'<i></i>':'')+'</span>';
function steps(labels,j,match,bar){return labels.map((l,i)=>{const on=j&&match;const failed=on&&!!j.err&&i===j.step,done=on&&(j.done||j.step>i),active=on&&!done&&!failed&&i===j.step&&j.running;
  let d=failed?j.err:(done||active)?(j.det[i]||''):'';if(active&&!d)d='WORKING'+dots();
  const b=active&&bar&&i===1?'<span style="width:160px;height:14px;border:2px solid var(--ink);padding:2px;display:flex"><i style="width:'+j.pct+'%;background:var(--ink);display:block"></i></span>':'';if(b)d=j.pct+'%';
  return '<div class="step"><span class="mk">'+(done?'<i class="dot done"></i>':active?'<i class="dot act"></i>':failed?'<span style="font-size:18px">×</span>':'<i class="dot pend"></i>')+'</span><span class="sf" style="flex:1;font-size:21px">'+l+'</span>'+b+'<span class="c11" style="min-width:110px;text-align:right">'+esc(d)+'</span></div>'}).join('')}

function render(force){
  const a=document.activeElement;if(!force&&a&&a.closest('#app')&&a.matches('input,select,textarea')){live();return}
  if(!S){$('#app').innerHTML=err?offline():'';return}
  const w=S.wifi,host=location.hostname;
  $('#stat').innerHTML='<span>VIA '+esc(host==='192.168.4.1'?S.hs.name:(w.connected||host))+' · '+esc(host)+'</span><span>'+(w.connected?'INTERNET VIA '+esc(w.connected.toUpperCase()):'NO INTERNET')+'</span>'+(S.dev.batt>=0?'<span>BATTERY '+S.dev.batt+'%'+(S.dev.usb?(S.dev.batt<100?' · CHARGING':' · FULL'):'')+'</span>':S.dev.usb?'<span>USB POWER</span>':'')+'<span>'+(S.dev.sd?'SD ':'FLASH ')+(S.dev.used/1e9).toFixed(1)+' OF '+(S.dev.total/1e9).toFixed(S.dev.sd?0:2)+' GB</span>';
  if(err){$('#app').innerHTML=offline();return}
  if(S.phone.active){$('#app').innerHTML=phone();return}
  phoneSent=false;
  if(selId===null&&S.recs.length)selId=S.recs[0].id;
  if(selId!==null&&!rec())selId=S.recs.length?S.recs[0].id:null;
  const r=rec();const k=r?r.id+':'+r.status+':'+S.repSeq:'';if(r&&k!==detKey){detKey=k;if(!det||det.id!==r.id)det=null;if(r.status===2)api('/api/rec/'+r.id).then(d=>{det=d;render(true)}).catch(()=>{})}
  const body=({recordings:vRecs,record:vRecord,wifi:vWifi,hotspot:vHotspot,whisper:vWhisper,reports:vReports,device:vDevice}[view]||vRecs)();
  $('#app').innerHTML='<div class="body">'+nav()+'<main>'+crumbs()+body+'</main></div>';
  live();
}
function offline(){return '<div style="min-height:70vh;display:flex;align-items:center;justify-content:center;padding:40px"><div style="max-width:440px"><div class="sf" style="font-size:44px;line-height:1.1">Device not reachable</div><div class="c13" style="line-height:1.7;margin-top:16px">THIS PAGE LOST ITS CONNECTION. CHECK THAT THE LOCAL NETWORK IS ON UNDER SETTINGS → LOCAL NETWORK, AND THAT THIS DEVICE IS STILL JOINED TO IT.</div><div style="margin-top:28px"><button class="btn" data-a="retry">TRY AGAIN</button></div></div></div>'}
function phone(){const p=S.phone,w=p.purpose==='w',sk=p.purpose==='K',md=p.purpose==='M',mi=S.wh.svc===2,key=!md&&(sk||S.wh.svc!==1);
  const head=w?'Password for '+esc(p.ssid):md?'Model to use':sk?'API key for your Whisper server':key?(mi?'Your Mistral API key':'Your OpenAI API key'):'Your Whisper server';
  const sub=w?'THE DEVICE JOINS THIS NETWORK AS SOON AS YOU SEND IT.':md?'NOW: '+esc(S.wh.modelName).toUpperCase()+' · FOR EXAMPLE '+(S.wh.svc===0?'GPT-4O-TRANSCRIBE':mi?'VOXTRAL-MINI-LATEST':'LARGE-V3'):key?'STORED ON THE DEVICE ONLY. USED TO SEND RECORDINGS TO WHISPER.':'FOR EXAMPLE 192.168.1.20:9000';
  return '<div style="max-width:560px;margin:0 auto;padding:40px 24px"><div class="c11" style="margin-bottom:20px">PHONE SETUP</div><div class="h1">'+head+'</div><div class="c12" style="margin-top:12px;line-height:1.6">'+sub+'</div>'+
  (phoneSent?'<div class="sf" style="font-size:24px;margin-top:36px">Sent. Look at the device.</div>':
  '<div style="display:flex;flex-wrap:wrap;gap:10px;margin-top:28px"><input id="pv" class="in" style="flex:1 1 240px" type="'+(w||key?'password':'text')+'" placeholder="'+(w?'Password':md?esc(S.wh.modelName):sk||mi?'API key':key?'sk-…':'Server address')+'" autocomplete="off"><button class="btn k" data-a="phoneSend">'+(w?'JOIN':'SAVE')+'</button></div>')+'</div>'}
function nav(){const n=S.recs.length,r=S.rec,w=S.wifi,wh=S.wh;
  const M=[['recordings','Recordings',String(n)],['record','Record',r.active?(r.paused?'PAUSED':'● '+fmt(r.t)):'']];
  const X=[['wifi','Wi-Fi',w.on&&w.connected?'ON':'OFF'],['hotspot','Local network',S.hs.on?'ON':'OFF'],['whisper','Whisper',wh.ok?'READY':'SET UP'],['reports','Report types',String(S.rtypes.length)],['device','Device','']];
  const b=(x,s)=>'<button class="nav'+(s?' s':'')+'" data-a="go" data-v="'+x[0]+'">'+mk(view===x[0])+'<span class="l">'+x[1]+'</span><span class="c11">'+esc(x[2])+'</span></button>';
  return '<div class="side">'+M.map(x=>b(x)).join('')+'<div style="font-size:11px;letter-spacing:.14em;padding:24px 0 8px 34px">SETTINGS</div>'+X.map(x=>b(x,1)).join('')+'</div>'}
function crumbs(){const r=rec();let c=[];
  if(view==='recordings'){c=[['RECORDINGS']];if(r){c.push([r.title.toUpperCase(),r.status===2&&tab!=='transcript'?'tabT':null]);c.push([r.status===2?tab.toUpperCase():r.status===1?'SENDING':'AUDIO'])}}
  else if(view==='record')c=[['RECORD']];
  else{c=[['SETTINGS'],[{wifi:'WI-FI',hotspot:'LOCAL NETWORK',whisper:'WHISPER',reports:'REPORT TYPES',device:'DEVICE'}[view]]];if(view==='wifi'&&joinFor)c=[['SETTINGS'],['WI-FI','unjoin'],['JOIN '+joinFor.toUpperCase()]]}
  return '<nav class="crumbs">'+c.map((x,i)=>(i?'<span>›</span>':'')+(i<c.length-1&&x[1]?'<button data-a="'+x[1]+'">'+esc(x[0])+'</button>':'<span class="p">'+esc(x[0])+'</span>')).join('')+'</nav>'}

function vRecs(){const n=S.recs.length,r=rec();
  let h='<div style="display:flex;flex-wrap:wrap;gap:40px;align-items:flex-start"><div class="list" style="flex:1 1 280px;max-width:380px;min-width:0"><div style="display:flex;align-items:baseline;justify-content:space-between;padding-bottom:12px;border-bottom:2px solid var(--ink)"><span class="sf" style="font-size:36px;letter-spacing:-.02em">Recordings</span><span class="c12">'+n+(n===1?' RECORDING':' RECORDINGS')+'</span></div>';
  if(!n)h+='<div class="c13" style="padding:24px 0">NO RECORDINGS YET</div>';
  h+=S.recs.map(x=>'<button class="it" data-a="sel" data-id="'+x.id+'">'+mk(x.id===selId)+'<span style="flex:1;min-width:0"><span class="sf" style="display:block;font-size:21px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">'+esc(x.title)+'</span><span class="c11" style="display:block;margin-top:5px">'+dstr(x.ts)+' · '+fmt(x.dur)+(x.rtype&&x.rtype!=='none'&&rtName(x.rtype)?' · '+esc(rtName(x.rtype)).toUpperCase():'')+'</span></span>'+(x.status===2?'<span class="tag">TEXT</span>':x.status===1?'<span class="tag d">SENDING</span>':'<span class="tag l">AUDIO</span>')+'</button>').join('')+'</div>';
  if(r)h+=detail(r);
  return h+'</div>'}
function detail(r){const wh=S.wh,sd=S.send;
  let h='<div style="flex:1 1 420px;min-width:0;max-width:760px"><input data-in="title" value="'+esc(r.title)+'" class="sf" style="width:100%;font-size:40px;letter-spacing:-.02em;background:transparent;border:0;border-bottom:2px solid transparent;padding:0 0 4px;outline:none" onfocus="this.style.borderBottomColor=\'#141414\'" onblur="this.style.borderBottomColor=\'transparent\'">';
  h+='<div class="c12" style="margin-top:10px">'+dstr(r.ts)+' · '+fmt(r.dur)+' · '+mb(r.dur)+(r.marks.length?' · '+r.marks.length+(r.marks.length>1?' MARKS':' MARK'):'')+' · '+(r.status===2?'TRANSCRIBED':r.status===1?'SENDING':'AUDIO ONLY')+'</div>'+recTypeRow(r);
  h+='<div style="margin-top:28px;border:2px solid var(--ink);padding:18px 20px;display:flex;flex-direction:column;gap:14px"><div style="display:flex;flex-wrap:wrap;align-items:center;gap:12px 16px">'+
   '<button id="pbtn" data-a="play" style="width:48px;height:48px;background:var(--ink);color:var(--paper);border:0;font-size:16px;cursor:pointer">▶</button>'+
   '<button class="btn sm" data-a="seek" data-d="-10">−10 S</button><button class="btn sm" data-a="seek" data-d="10">+10 S</button>'+
   '<span id="ppos" style="font-size:22px;letter-spacing:-.02em;font-variant-numeric:tabular-nums">00:00</span><span class="c12">OF '+fmt(r.dur)+'</span>'+
   '<span style="margin-left:auto;display:flex;align-items:center;gap:8px" class="c11"><span>PLAY ON</span>'+[['browser','THIS BROWSER'],['device','DEVICE']].map(o=>'<button class="btn sm'+(playOn===o[0]?' k':'')+'" style="height:28px;padding:0 8px" data-a="playOn" data-v="'+o[0]+'">'+o[1]+'</button>').join('')+'</span></div>'+
   '<div data-a="scrub" style="cursor:pointer;padding-top:12px;position:relative">'+r.marks.map(m=>'<div style="position:absolute;top:0;left:'+(m/Math.max(1,r.dur)*100)+'%;width:3px;height:9px;background:var(--ink)"></div>').join('')+'<div class="bar"><i id="pbar" style="width:0%"></i></div></div>'+
   '<div class="c11">'+(playOn==='browser'?'STREAMING FROM THE DEVICE TO THIS BROWSER':'PLAYING ON THE DEVICE THROUGH THE BUILT-IN SPEAKER')+'</div></div>';
  h+='<div style="display:flex;flex-wrap:wrap;gap:10px;margin-top:20px">'+(r.status===0?'<button class="btn k" data-a="send">SEND TO WHISPER</button>':'')+
   '<a class="btn" href="/api/rec/'+r.id+'/audio?dl=1">DOWNLOAD AUDIO</a>'+(r.status===2?'<a class="btn" href="/api/rec/'+r.id+'/text">DOWNLOAD TEXT</a><button class="btn" data-a="send">TRANSCRIBE AGAIN</button>':'')+
   (confirmDel?'<span style="display:flex;align-items:center;gap:10px;padding-left:8px" class="c12"><span>DELETE FROM DEVICE?</span><button class="btn k" data-a="del">DELETE</button><button class="btn" data-a="nodel">CANCEL</button></span>':'<button class="btn" data-a="askdel">DELETE</button>')+'</div>';
  const sending=r.status===1||(sd.id===r.id&&sd.err&&!sd.running);
  if(sending)h+='<div style="margin-top:36px;border-top:2px solid var(--ink)">'+steps(['Connect to Wi-Fi','Upload audio','Transcribe','Summarise'],sd,sd.id===r.id,true)+'</div>';
  else if(r.status===0)h+='<div style="margin-top:36px;padding:28px 0;border-top:2px solid var(--ink)"><div class="sf" style="font-size:24px">Not transcribed yet</div><div class="c12" style="margin-top:8px;line-height:1.6">'+(wh.ok?'SEND IT TO WHISPER TO GET A TRANSCRIPT AND SUMMARY.':'SET UP WHISPER UNDER SETTINGS FIRST, THEN SEND IT FOR A TRANSCRIPT AND SUMMARY.')+'</div></div>';
  if(r.status===2){h+='<div style="margin-top:36px;display:flex;gap:24px;border-bottom:2px solid var(--ink)">'+[['transcript','TRANSCRIPT'],['summary','SUMMARY'],['reports','REPORTS'+(det&&det.reports&&det.reports.length?' · '+det.reports.length:'')]].map(t=>'<button data-a="tab" data-v="'+t[0]+'" style="padding:0 0 10px;background:none;border:0;border-bottom:2px solid '+(tab===t[0]?'var(--ink)':'transparent')+';margin-bottom:-2px;font-size:12px;letter-spacing:.14em;cursor:pointer">'+t[1]+'</button>').join('')+'</div>';
    if(!det)h+='<div class="c12" style="padding-top:20px">LOADING…</div>';
    else if(tab==='reports')h+=vRecReports(r);
    else if(tab==='transcript')h+=vTranscript();
    else h+='<div style="padding-top:20px;max-width:62ch"><div class="c11" style="margin-bottom:10px">SUMMARY</div>'+((det.sum||[]).map(x=>'<div class="sf" style="display:flex;gap:14px;font-size:19px;line-height:1.6;margin-bottom:10px"><span>—</span><span>'+esc(x)+'</span></div>').join('')||'<div class="c12">NO SUMMARY</div>')+
      '<div class="c11" style="margin:28px 0 10px">ACTION ITEMS</div>'+((det.act||[]).map(x=>'<div class="sf" style="display:flex;gap:14px;font-size:19px;line-height:1.6;margin-bottom:10px"><span style="width:10px;height:10px;border:2px solid var(--ink);flex:none;margin-top:9px"></span><span>'+esc(x)+'</span></div>').join('')||'<div class="c12">NONE</div>')+'</div>'}
  return h+'</div>'}
// speakers: det.names[n-1] is the name of speaker n ("" = not named), det.segs[i][2] the speaker of a paragraph
function spkN(n){return (det.names&&det.names[n-1])||'Speaker '+n}
function vTranscript(){const names=det.names;let prev=0;
  let h=names&&names.length?'<div style="padding-top:20px"><div class="c11">SPEAKERS · TAP ONE TO GIVE IT A NAME · SAME NAME TWICE JOINS THEM</div><div style="display:flex;flex-wrap:wrap;gap:8px;margin-top:10px">'+names.map((x,i)=>'<button class="btn sm'+(x?' k':'')+'" data-a="spkName" data-n="'+(i+1)+'">'+esc(spkN(i+1).toUpperCase())+'</button>').join('')+'</div></div>':'';
  return h+'<div style="padding-top:20px;display:flex;flex-direction:column;gap:20px">'+(det.segs||[]).map(g=>{const sp=names?g[2]||0:0,lab=sp&&sp!==prev?'<button data-a="spkName" data-n="'+sp+'" class="c11" style="display:block;padding:0 0 4px;background:none;border:0;cursor:pointer;text-align:left">'+esc(spkN(sp).toUpperCase())+'</button>':'';prev=sp;
    return '<div style="display:flex;gap:18px;flex-wrap:wrap"><button data-a="seekTo" data-t="'+g[0]+'" class="c11" style="flex:0 0 96px;text-align:left;padding:'+(lab?'25px':'5px')+' 0 0;background:none;border:0;line-height:1.6;cursor:pointer">'+fmt(g[0])+'</button><div style="flex:1 1 300px;max-width:62ch">'+lab+'<div class="sf" style="font-size:19px;line-height:1.6">'+esc(g[1])+'</div></div></div>'}).join('')+'</div>'}
function vRecord(){const r=S.rec;
  return '<div style="max-width:720px"><div class="h1">Record</div><div class="c12" style="margin-top:10px">USES THE MICROPHONE ON THE DEVICE</div><div style="margin-top:40px;border-top:2px solid var(--ink);padding-top:24px">'+
  '<div style="display:flex;align-items:center;gap:12px;font-size:13px;letter-spacing:.14em"><span style="width:14px;height:14px;border-radius:50%;border:2px solid var(--ink);background:'+(r.active&&!r.paused?'var(--ink)':'transparent')+'"></span><span>'+(r.active?(r.paused?'PAUSED':'RECORDING ON DEVICE'):'READY')+'</span></div>'+
  '<div style="font-size:clamp(72px,14vw,140px);letter-spacing:-.06em;line-height:1;margin-top:20px;font-variant-numeric:tabular-nums">'+fmt(r.t)+'</div>'+
  '<div style="height:100px;display:flex;align-items:center;gap:4px;margin-top:28px;overflow:hidden">'+r.lv.map(v=>'<div style="flex:0 0 6px;height:'+Math.max(4,v*0.96)+'px;background:var(--ink)"></div>').join('')+'</div>'+
  '<div class="c12" style="margin-top:16px">'+(r.marks.length?r.marks.length+(r.marks.length>1?' MARKS · LAST AT ':' MARK AT ')+fmt(r.marks[r.marks.length-1]):'NO MARKS')+'</div>'+
  '<div style="display:flex;flex-wrap:wrap;gap:10px;margin-top:28px">'+(r.active?'<button class="btn lg" data-a="rec" data-v="pause">'+(r.paused?'RESUME':'PAUSE')+'</button><button class="btn lg" data-a="rec" data-v="mark">ADD MARK</button><button class="btn lg k" data-a="rec" data-v="stop">STOP AND SAVE</button>':'<button class="btn lg k" data-a="rec" data-v="start">START RECORDING</button>')+'</div></div></div>'}
function vWifi(){const w=S.wifi,pg=S.prog;
  let h='<div style="max-width:720px"><div class="h1">Wi-Fi</div><div class="c12" style="margin-top:10px">'+(!w.on?'OFF · RECORDINGS STAY ON THE DEVICE':w.connected?'CONNECTED TO '+esc(w.connected.toUpperCase()):'NOT CONNECTED')+'</div>'+
  '<div class="row" style="margin-top:28px;border-top:2px solid var(--ink)"><div style="flex:1"><div class="rt">Wi-Fi</div><div class="rs">NEEDED TO SEND RECORDINGS TO WHISPER</div></div><button class="btn sm'+(w.on?' k':'')+'" data-a="wifi" data-v="toggle">'+(w.on?'ON':'OFF')+'</button></div>';
  if(!w.on)return h+'</div>';
  const joining=pg.running&&pg.kind==='j'?pg.ssid:null;
  h+='<div class="sec">SAVED NETWORKS · JOINED IN THIS ORDER</div>'+w.nets.map((n,i)=>{const c=n.ssid===w.connected;
    return '<div class="row" style="padding:14px 0"><span style="font-size:12px;width:22px;text-align:center">'+(i+1)+'</span><div style="flex:1 1 200px;min-width:0"><div class="rt" style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis">'+esc(n.ssid)+'</div><div class="rs">'+(joining===n.ssid?'CONNECTING':c?'CONNECTED · '+n.sec:n.inRange?'SAVED · '+n.sec:'OUT OF RANGE')+'</div></div>'+sig(n.sig)+
    '<span style="display:flex;gap:6px"><button class="btn sm" style="width:32px;padding:0;justify-content:center" data-a="wifi" data-v="move" data-i="'+i+'" data-d="-1" '+(i===0?'disabled':'')+' title="Join earlier">↑</button><button class="btn sm" style="width:32px;padding:0;justify-content:center" data-a="wifi" data-v="move" data-i="'+i+'" data-d="1" '+(i===w.nets.length-1?'disabled':'')+' title="Join later">↓</button>'+
    '<button class="btn sm" data-a="wifi" data-v="'+(c?'disconnect':'connect')+'" data-i="'+i+'">'+(c?'DISCONNECT':'CONNECT')+'</button><button class="btn sm" data-a="wifi" data-v="forget" data-i="'+i+'">FORGET</button></span></div>'}).join('');
  if(pg.kind==='j'&&(pg.running||pg.err))h+='<div style="margin-top:12px">'+steps(['Join network','Get IP address','Check internet'],pg,true,false)+'</div>';
  h+='<div style="display:flex;align-items:center;justify-content:space-between;margin-top:40px;padding-bottom:8px;border-bottom:2px solid var(--ink)"><span class="c11" style="letter-spacing:.14em">ADD A NETWORK</span><button class="btn sm" data-a="wifi" data-v="scan">'+(w.scanning?'SEARCHING'+dots():w.scan.length?'SEARCH AGAIN':'SEARCH')+'</button></div>';
  h+=(w.scanning?[]:w.scan).map(n=>'<div style="border-bottom:1px solid var(--ink)"><button class="it" data-a="pick" data-s="'+esc(n.ssid)+'" style="width:100%;display:flex;align-items:center;gap:16px;padding:14px 0 14px 38px;background:none;border:0;cursor:pointer;text-align:left"><span style="flex:1;min-width:0"><span class="sf" style="display:block;font-size:21px">'+esc(n.ssid)+'</span><span class="c11" style="display:block;margin-top:4px">'+(joining===n.ssid?'CONNECTING':n.saved?'SAVED · CLICK TO JOIN':n.sec==='OPEN'?'OPEN · NO PASSWORD':n.sec+' · NEEDS PASSWORD')+'</span></span>'+sig(n.sig)+'</button>'+
    (joinFor===n.ssid?'<div style="display:flex;flex-wrap:wrap;align-items:center;gap:10px;padding:0 0 18px 38px"><input id="jpw" class="in" style="flex:1 1 220px" type="'+(showPw?'text':'password')+'" placeholder="Password"><button class="btn" data-a="showPw">'+(showPw?'HIDE':'SHOW')+'</button><button class="btn k" data-a="join" data-s="'+esc(n.ssid)+'" data-e="'+n.sec+'">JOIN</button></div>':'')+'</div>').join('');
  h+='<div style="display:flex;flex-wrap:wrap;align-items:center;gap:10px;padding:18px 0 0 38px"><span class="c11" style="flex:0 0 100%">HIDDEN NETWORK</span><input id="hs" class="in" style="flex:1 1 180px" placeholder="Network name"><input id="hp" class="in" style="flex:1 1 180px" type="password" placeholder="Password"><button class="btn" data-a="hidJoin">JOIN</button></div>';
  return h+'</div>'}
function vHotspot(){const hs=S.hs;if(!hsDraft)hsDraft={name:hs.name,pw:hs.pw};const dirty=hsDraft.name!==hs.name||hsDraft.pw!==hs.pw;
  return '<div style="max-width:720px"><div class="h1">Local network</div><div class="c12" style="margin-top:10px;line-height:1.6">THE DEVICE\'S OWN WI-FI. JOIN IT FROM A PHONE OR LAPTOP AND OPEN 192.168.4.1.</div>'+
  '<div class="row" style="margin-top:28px;border-top:2px solid var(--ink)"><div style="flex:1 1 240px"><div class="rt">Local network</div><div class="rs">WI-FI STAYS CONNECTED WHILE THIS IS ON</div></div>'+
  (hsConfirm?'<span style="display:flex;flex-wrap:wrap;align-items:center;gap:8px" class="c11"><span>'+(location.hostname==='192.168.4.1'?'THIS PAGE WILL DISCONNECT':'PHONES ON IT WILL DISCONNECT')+'</span><button class="btn sm k" data-a="hsOff">TURN OFF</button><button class="btn sm" data-a="hsCancel">CANCEL</button></span>':
   hs.on?'<button class="btn sm k" data-a="hsAsk">ON</button>':'<button class="btn sm" data-a="hsOn">OFF</button>')+'</div>'+
  '<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:20px 28px;margin-top:28px"><label class="c11" style="display:flex;flex-direction:column;gap:8px">NETWORK NAME<input class="in" data-in="hsName" value="'+esc(hsDraft.name)+'"></label>'+
  '<label class="c11" style="display:flex;flex-direction:column;gap:8px">PASSWORD<span style="display:flex;gap:8px"><input class="in" style="flex:1" data-in="hsPw" type="'+(hsShow?'text':'password')+'" value="'+esc(hsDraft.pw)+'"><button class="btn" data-a="hsShow">'+(hsShow?'HIDE':'SHOW')+'</button><button class="btn" data-a="hsGen">NEW</button></span></label></div>'+
  '<div style="display:flex;flex-wrap:wrap;align-items:center;gap:12px 20px;margin-top:20px"><button class="btn'+(dirty?' k':'')+'" data-a="hsApply">APPLY CHANGES</button><span class="c11">'+(dirty?'PHONES ON IT RECONNECT WITH THE NEW DETAILS':'NO CHANGES')+'</span></div>'+
  '<div class="sec" style="margin-top:40px">ADDRESS</div><div style="padding:14px 0;border-bottom:1px solid var(--ink);display:flex;flex-wrap:wrap;gap:6px 24px;font-size:15px"><span>'+esc(S.url)+'</span>'+(S.ip?'<span>http://'+esc(S.ip)+'</span>':'')+'<span>http://192.168.4.1</span></div>'+
  '<div class="sec" style="margin-top:40px">CONNECTED NOW</div><div class="row" style="padding:14px 0"><span class="sf" style="flex:1;font-size:21px">This browser</span><span class="c11">'+esc(S.client)+'</span><span class="tag">THIS BROWSER</span></div>'+
  (hs.clients>1||(hs.clients===1&&location.hostname!=='192.168.4.1')?'<div class="row" style="padding:14px 0"><span class="sf" style="flex:1;font-size:21px">'+(location.hostname==='192.168.4.1'?hs.clients-1:hs.clients)+' other device(s) on the local network</span></div>':'')+'</div>'}
function modelField(wh){const sug=wh.svc===0?['whisper-1','gpt-4o-transcribe','gpt-4o-mini-transcribe']:wh.svc===2?['voxtral-mini-latest']:['large-v3','medium','small','Systran/faster-whisper-large-v3','deepdml/faster-whisper-large-v3-turbo-ct2','distil-large-v3'];
  return '<label class="c11" style="display:flex;flex-direction:column;gap:8px">MODEL · '+(wh.modelTyped?'TYPED':'DEFAULT')+'<input class="in" data-in="modelName" list="mlist" value="'+esc(wh.modelTyped)+'" placeholder="'+esc(wh.modelName)+'" autocomplete="off" spellcheck="false"><datalist id="mlist">'+sug.map(m=>'<option value="'+esc(m)+'">').join('')+'</datalist></label>'}
function vWhisper(){const wh=S.wh,pg=S.prog;
  let h='<div style="max-width:720px"><div class="h1">Whisper</div><div class="c12" style="margin-top:10px">'+(wh.ok?'CONNECTED · READY TO TRANSCRIBE':(wh.svc===0?wh.keySet:wh.svc===2?wh.miKeySet:wh.server)?'NOT TESTED YET':'NOT SET UP')+'</div>'+
  '<div class="sec">SERVICE</div><div style="display:flex;flex-wrap:wrap;gap:10px;padding-top:16px">'+[['OpenAI Whisper API','NEEDS AN API KEY · PAY PER MINUTE · INCLUDES SUMMARY'],['Self-hosted server','OPENAI-COMPATIBLE WHISPER ON YOUR OWN COMPUTER'],['Mistral Voxtral','EU · LABELS SPEAKERS · NEEDS A MISTRAL API KEY']].map((o,i)=>'<button data-a="svc" data-v="'+i+'" style="flex:1 1 220px;text-align:left;padding:14px 16px;background:transparent;border:'+(wh.svc===i?'3px':'1px')+' solid var(--ink);cursor:pointer"><span class="sf" style="display:block;font-size:21px">'+o[0]+'</span><span class="c11" style="display:block;margin-top:6px">'+o[1]+'</span></button>').join('')+'</div>'+
  '<div class="sec">CONNECTION</div><div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:20px 28px;padding-top:18px">';
  if(wh.svc===0)h+='<label class="c11" style="display:flex;flex-direction:column;gap:8px;grid-column:1/-1">API KEY'+(wh.keySet?' · SAVED ('+esc(wh.keyHint)+')':'')+'<span style="display:flex;gap:8px"><input class="in" style="flex:1" data-in="key" type="'+(showKey?'text':'password')+'" placeholder="'+(wh.keySet?'Type a new key to replace it':'sk-…')+'" autocomplete="off"><button class="btn" data-a="showKey">'+(showKey?'HIDE':'SHOW')+'</button></span></label>'+modelField(wh);
  else if(wh.svc===2)h+='<label class="c11" style="display:flex;flex-direction:column;gap:8px;grid-column:1/-1">MISTRAL API KEY'+(wh.miKeySet?' · SAVED ('+esc(wh.miKeyHint)+')':' · FROM CONSOLE.MISTRAL.AI')+'<span style="display:flex;gap:8px"><input class="in" style="flex:1" data-in="miKey" type="'+(showKey?'text':'password')+'" placeholder="'+(wh.miKeySet?'Type a new key to replace it':'API key')+'" autocomplete="off"><button class="btn" data-a="showKey">'+(showKey?'HIDE':'SHOW')+'</button></span></label>'+modelField(wh)+
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px">SPEAKERS<select class="in" data-in="spk"><option value="1"'+(wh.spk?' selected':'')+'>Label speakers</option><option value="0"'+(wh.spk?'':' selected')+'>Off</option></select></label>';
  else h+='<label class="c11" style="display:flex;flex-direction:column;gap:8px">SERVER ADDRESS<input class="in" data-in="server" value="'+esc(wh.server)+'" placeholder="192.168.1.20:9000"></label>'+modelField(wh)+
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px;grid-column:1/-1">API KEY · OPTIONAL'+(wh.srvKeySet?' · SAVED ('+esc(wh.srvKeyHint)+')':' · ONLY IF YOUR SERVER ASKS FOR ONE')+'<span style="display:flex;gap:8px"><input class="in" style="flex:1" data-in="srvKey" type="'+(showKey?'text':'password')+'" placeholder="'+(wh.srvKeySet?'Type a new key to replace it':'Bearer token for your server')+'" autocomplete="off"><button class="btn" data-a="showKey">'+(showKey?'HIDE':'SHOW')+'</button>'+(wh.srvKeySet?'<button class="btn" data-a="srvKeyClear">REMOVE</button>':'')+'</span></label>';
  h+=wh.svc===2&&wh.spk?'<div class="c11" style="display:flex;flex-direction:column;gap:8px">LANGUAGE<span style="line-height:1.6;padding-top:6px">DETECTED AUTOMATICALLY · MISTRAL CANNOT FIX THE LANGUAGE WHILE IT LABELS SPEAKERS</span></div></div>':
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px">LANGUAGE<select class="in" data-in="lang">'+LANGS.map((m,i)=>'<option value="'+i+'"'+(wh.lang===i?' selected':'')+'>'+m+'</option>').join('')+'</select></label></div>';
  h+=
  '<div style="display:flex;flex-wrap:wrap;align-items:center;gap:12px 20px;margin-top:24px"><button class="btn k" data-a="test">TEST CONNECTION</button><span class="c11">SENDS A 2-SECOND SAMPLE</span></div>';
  if(pg.kind==='t')h+='<div style="margin-top:24px;border-top:2px solid var(--ink)">'+steps(['Reach server',wh.svc!==1?'Check API key':'Check server','Test transcription'],pg,true,false)+'</div>';
  return h+'</div>'}
function vDevice(){const d=S.dev,u=d.total?Math.round(d.used/d.total*100):0;
  const tg=(on,a)=>'<button class="btn sm'+(on?' k':'')+'" data-a="'+a+'">'+(on?'ON':'OFF')+'</button>';
  return '<div style="max-width:720px"><div class="h1">Device</div><div class="c12" style="margin-top:10px">ESP32-S3-EPAPER-3.97 · FIRMWARE '+esc(S.fw)+'</div>'+
  '<div class="sec">RECORDING</div><div class="row"><div style="flex:1"><div class="rt">Send after recording</div><div class="rs">TRANSCRIBE AUTOMATICALLY WHEN WI-FI AND WHISPER ARE READY</div></div>'+tg(d.auto,'auto')+'</div>'+
  '<div class="row"><div style="flex:1 1 260px"><div class="rt">Speaker volume</div><div class="rs">BUILT-IN SPEAKER</div></div><input type="range" min="0" max="100" step="5" value="'+d.vol+'" data-in="vol" style="width:180px;accent-color:#141414"></div>'+
  '<div class="sec">POWER</div><div class="row"><div style="flex:1 1 260px"><div class="rt">Sleep after</div><div class="rs">WITHOUT USE · NEVER WHILE ON A CHARGER, RECORDING OR SENDING · A ROCKER PRESS OR PLUGGING IN WAKES IT</div></div><select class="in" data-in="sleepMin" style="flex:0 1 200px">'+[2,5,10,15,30,60,0].map(m=>'<option value="'+m+'"'+(d.sleepMin===m?' selected':'')+'>'+(m?m+' minutes':'Never')+'</option>').join('')+'</select></div>'+
  '<div class="row"><div style="flex:1"><div class="rt">Battery</div><div class="rs">'+(d.batt>=0?d.batt+'% · '+(d.mv/1000).toFixed(2)+' V':'NO BATTERY FOUND')+(d.usb?' · ON USB POWER'+(d.batt>=0&&d.batt<100?', CHARGING':''):' · ON BATTERY')+'</div></div></div>'+
  '<div class="sec">DISPLAY</div><div class="row"><div style="flex:1 1 260px"><div class="rt">Full refresh</div><div class="rs">AFTER THIS MANY PARTIAL UPDATES, ON THE NEXT SCREEN CHANGE</div></div><div style="display:flex;align-items:center;border:2px solid var(--ink)"><button data-a="full" data-d="-5" style="width:36px;height:32px;background:none;border:0;border-right:2px solid var(--ink);cursor:pointer">−</button><span style="width:56px;text-align:center;font-size:15px">'+d.fullEvery+'</span><button data-a="full" data-d="5" style="width:36px;height:32px;background:none;border:0;border-left:2px solid var(--ink);cursor:pointer">+</button></div></div>'+
  '<div class="row"><div style="flex:1"><div class="rt">Clear screen now</div><div class="rs">REMOVES GHOSTING · '+d.partials+' PARTIAL · '+d.fulls+' FULL SINCE BOOT</div></div><button class="btn sm" data-a="clear">CLEAR</button></div>'+
  '<div class="sec">STORAGE</div><div style="padding:16px 0;border-bottom:1px solid var(--ink)"><div class="c12" style="display:flex;justify-content:space-between"><span>'+(d.used/1e9).toFixed(2)+' OF '+(d.total/1e9).toFixed(1)+' GB USED · '+(d.sd?'SD CARD':'INTERNAL FLASH')+'</span><span>'+S.recs.length+' RECORDINGS</span></div><div class="bar" style="margin-top:10px"><i style="width:'+u+'%"></i></div></div>'+
  '<div class="sec">FIRMWARE</div>'+otaRows()+(S.ota&&S.ota.rolledBack?'<div class="row"><div style="flex:1"><div class="rt">Last update did not start</div><div class="rs">THE DEVICE WENT BACK TO THE PREVIOUS FIRMWARE BY ITSELF</div></div></div>':'')+
  '<div class="row"><div style="flex:1 1 260px"><div class="rt">Install from a file</div><div class="rs">'+(fwUp===null?'VERSION '+esc(S.fw)+' · SLOT '+esc((S.ota&&S.ota.slot||'').toUpperCase())+' · CHOOSE A .BIN FILE, THE DEVICE RESTARTS WHEN IT IS IN':fwUp==='done'?'DONE · RESTARTING, BACK IN ABOUT 15 S':'UPLOADING · '+fwUp+'% · KEEP THIS PAGE OPEN')+'</div></div>'+
  (fwUp===null?'<label class="btn sm" style="cursor:pointer">UPLOAD .BIN<input type="file" accept=".bin" data-in="fw" style="display:none"></label>':'<span style="width:160px;height:14px;border:2px solid var(--ink);padding:2px;display:flex"><i style="width:'+(fwUp==='done'?100:fwUp)+'%;background:var(--ink);display:block"></i></span>')+'</div>'+
  '<div class="row"><div style="flex:1"><div class="rt">Restart device</div><div class="rs">RECORDINGS AND SETTINGS ARE KEPT</div></div><button class="btn sm" data-a="restart">RESTART</button></div></div>'}

// ---------- reports ----------
// small Markdown renderer for AI output: headings, lists, tables, bold/italic, paragraphs
function md(t){const L=String(t||'').replace(/\r/g,'').split('\n');let o='',list=null,tbl=null;
  const inl=x=>esc(x).replace(/\*\*(.+?)\*\*/g,'<b>$1</b>').replace(/(^|[^*])\*(?!\s)(.+?)\*/g,'$1<i>$2</i>').replace(/`(.+?)`/g,'$1');
  const close=()=>{if(list){o+='</'+list+'>';list=null}if(tbl){o+='</table></div>';tbl=null}};
  for(let l of L){const x=l.trim();
    if(!x){close();continue}
    let m;
    if(/^\|.*\|$/.test(x)){if(/^\|[\s:|-]+\|$/.test(x))continue;const c=x.slice(1,-1).split('|').map(v=>v.trim());
      if(!tbl){close();tbl=1;o+='<div style="overflow-x:auto;margin:10px 0 16px"><table style="border-collapse:collapse;width:100%;font-size:15px">';o+='<tr>'+c.map(v=>'<th style="text-align:left;border-bottom:2px solid var(--ink);padding:6px 10px 6px 0;font-size:11px;letter-spacing:.12em;font-weight:400">'+inl(v).toUpperCase()+'</th>').join('')+'</tr>'}
      else o+='<tr>'+c.map(v=>'<td class="sf" style="border-bottom:1px solid var(--ink);padding:8px 10px 8px 0;vertical-align:top">'+inl(v)+'</td>').join('')+'</tr>';continue}
    if(tbl){o+='</table></div>';tbl=null}
    if(m=x.match(/^(#{1,4})\s+(.*)$/)){close();const n=m[1].length;
      o+=n===1?'<div class="sf" style="font-size:30px;letter-spacing:-.01em;margin:8px 0 10px">'+inl(m[2])+'</div>':n===2?'<div class="c11" style="margin:26px 0 10px;padding-bottom:6px;border-bottom:2px solid var(--ink)">'+inl(m[2]).toUpperCase()+'</div>':'<div class="sf" style="font-size:21px;margin:18px 0 6px">'+inl(m[2])+'</div>';continue}
    if(m=x.match(/^[-*]\s+(.*)$/)){if(list!=='ul'){close();list='ul';o+='<ul style="margin:6px 0 12px;padding-left:22px">'}o+='<li class="sf" style="font-size:18px;line-height:1.6;margin-bottom:4px">'+inl(m[1])+'</li>';continue}
    if(m=x.match(/^\d+[.)]\s+(.*)$/)){if(list!=='ol'){close();list='ol';o+='<ol style="margin:6px 0 12px;padding-left:26px">'}o+='<li class="sf" style="font-size:18px;line-height:1.6;margin-bottom:4px">'+inl(m[1])+'</li>';continue}
    if(/^-{3,}$/.test(x)){close();o+='<div style="border-top:1px solid var(--ink);margin:18px 0"></div>';continue}
    close();o+='<p class="sf" style="font-size:18px;line-height:1.6;margin:0 0 10px;max-width:68ch">'+inl(x)+'</p>'}
  close();return o}
const rtName=id=>{const t=S.rtypes.find(x=>x[0]===id);return t?t[1]:''};
function recTypeRow(r){const def=S.rauto&&rtName(S.rauto),cur=r.rtype||'',eff=cur==='none'?'':(cur&&rtName(cur)?cur:S.rauto),reps=(det&&det.reports)||[],J=S.rep,busy=J.running&&J.id===r.id;
  const opts=[['',def?'Default ('+def+')':'Default (none)'],['none','No report']].concat(S.rtypes.map(t=>[t[0],t[1]]));
  let note=r.status!==2?(eff?'WRITTEN AFTER TRANSCRIBING':'NO REPORT AFTER TRANSCRIBING'):!eff?'':reps.some(x=>x.type===eff)?'REPORT IS IN THE REPORTS TAB':'';
  const btn=r.status===2&&eff&&!reps.some(x=>x.type===eff)&&det?'<button class="btn sm k" data-a="repMakeType" data-t="'+esc(eff)+'"'+(busy||!S.aiReady?' disabled':'')+'>'+(busy?'WRITING'+dots():'CREATE '+esc(rtName(eff)).toUpperCase())+'</button>':'';
  return '<div style="display:flex;flex-wrap:wrap;align-items:center;gap:10px 14px;margin-top:18px"><span class="c11">REPORT TYPE</span><select class="in" data-in="recType" style="height:36px;flex:0 1 260px;font-size:14px">'+opts.map(o=>'<option value="'+esc(o[0])+'"'+(o[0]===cur?' selected':'')+'>'+esc(o[1])+'</option>').join('')+'</select>'+btn+(note?'<span class="c11">'+note+'</span>':'')+'</div>'}
function vRecReports(r){const reps=(det&&det.reports)||[],J=S.rep,busy=J.running&&J.id===r.id,types=S.rtypes;
  const pref=r.rtype&&r.rtype!=='none'&&types.some(t=>t[0]===r.rtype)?r.rtype:(S.rauto&&types.some(t=>t[0]===S.rauto))?S.rauto:(types[0]||[''])[0];
  if(repSel===null||repFor!==r.id||!types.some(t=>t[0]===repSel)){repSel=pref;repFor=r.id}
  const has=reps.some(x=>x.type===repSel);
  let h='<div style="padding-top:20px">';
  if(!S.aiReady)h+='<div style="border:2px solid var(--ink);padding:16px 18px;margin-bottom:20px"><div class="sf" style="font-size:21px">No AI set up for reports</div><div class="c11" style="margin-top:8px;line-height:1.6">ADD AN OPENAI KEY UNDER WHISPER, OR SET AN AI SERVER UNDER REPORT TYPES.</div><button class="btn sm" style="margin-top:12px" data-a="go" data-v="reports">OPEN REPORT TYPES</button></div>';
  h+='<div style="display:flex;flex-wrap:wrap;align-items:center;gap:10px"><select class="in" id="repSel" data-in="repSel" style="flex:1 1 220px">'+types.map(t=>'<option value="'+esc(t[0])+'"'+(t[0]===repSel?' selected':'')+'>'+esc(t[1])+'</option>').join('')+'</select>'+
     '<button class="btn k" data-a="repMake"'+(busy||!S.aiReady?' disabled':'')+'>'+(busy?'WRITING'+dots():has?'WRITE AGAIN':'CREATE REPORT')+'</button></div>';
  if(busy)h+='<div class="c11" style="margin-top:12px">WRITING '+esc(J.det[0])+dots()+' · THIS CAN TAKE A MINUTE FOR LONG RECORDINGS</div>';
  else if(J.id===r.id&&J.err)h+='<div class="c11" style="margin-top:12px;line-height:1.6">COULD NOT WRITE '+esc(J.det[0])+' · '+esc(J.err)+'</div>';
  if(!reps.length&&!busy)h+='<div class="c12" style="padding:28px 0">NO REPORTS YET · PICK A TYPE AND CREATE ONE</div>';
  reps.forEach(x=>{h+='<div style="margin-top:32px;border-top:2px solid var(--ink);padding-top:16px"><div style="display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px"><span class="sf" style="flex:1 1 200px;font-size:26px">'+esc(x.name)+'</span><span class="c11">'+(x.ts?dstr(x.ts):'')+'</span></div>'+
    '<div style="display:flex;flex-wrap:wrap;gap:8px;margin:12px 0 18px"><a class="btn sm" href="/api/rec/'+r.id+'/report/dl?type='+encodeURIComponent(x.type)+'">DOWNLOAD</a><button class="btn sm" data-a="repCopy" data-t="'+esc(x.type)+'">COPY TEXT</button>'+
    (repConfirm===x.type?'<span class="c11" style="display:flex;align-items:center;gap:8px">DELETE THIS REPORT?<button class="btn sm k" data-a="repDelYes" data-t="'+esc(x.type)+'">DELETE</button><button class="btn sm" data-a="repDelNo">CANCEL</button></span>':'<button class="btn sm" data-a="repDel" data-t="'+esc(x.type)+'">DELETE</button>')+'</div>'+
    '<div>'+md(x.text)+'</div></div>'});
  return h+'</div>'}
function loadRT(){api('/api/reports').then(d=>{RT=d;render(true)}).catch(e=>toast(e.message))}
const NEW_PROMPT='You write a <kind of document> from a transcript of a recording.\n\nINPUT\nYou get the recording title, date and duration, followed by the transcript. Each paragraph starts with a timestamp [mm:ss].\n\nOUTPUT\nWrite in the same language as the transcript, in Markdown, with these sections:\n\n# <title>\n## <section 1>\n## <section 2>\n\nRULES\n- Be factual and concise.\n- Never invent names, numbers or decisions. Write [unclear] where the transcript is unclear.';
function vReports(){
  if(!RT){loadRT();return '<div class="c12">LOADING…</div>'}
  const ai=RT.ai;
  let h='<div style="max-width:820px"><div class="h1">Report types</div><div class="c12" style="margin-top:10px;line-height:1.6">THE AI TURNS A TRANSCRIPT INTO ONE OF THESE REPORTS. EDIT THE PROMPTS OR ADD YOUR OWN TYPES.</div>';
  h+='<div class="sec">TYPES</div>';
  const editor=(t,isNew)=>{const d=rtDraft;
    return '<div style="padding:4px 0 22px"><div style="display:flex;flex-direction:column;gap:14px">'+
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px">NAME'+(t&&t.builtin?' · BUILT IN':'')+'<input class="in" data-in="rtName" value="'+esc(d.name)+'"'+(t&&t.builtin?' disabled':'')+' placeholder="For example: Customer visit"></label>'+
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px">PROMPT · WHAT THE AI IS TOLD TO DO<textarea class="in" data-in="rtPrompt" spellcheck="false" style="height:auto;min-height:340px;padding:10px;font-size:13px;line-height:1.55;resize:vertical">'+esc(d.prompt)+'</textarea></label>'+
    '<div class="c11" style="line-height:1.6">THE AI ALSO GETS THE TITLE, DATE, DURATION, MARKS AND THE TIMESTAMPED TRANSCRIPT. ASK FOR MARKDOWN (# HEADINGS, - LISTS, TABLES) FOR THE BEST LAYOUT.</div>'+
    '<div style="display:flex;flex-wrap:wrap;gap:10px;align-items:center"><button class="btn k" data-a="rtSave">SAVE</button><button class="btn" data-a="rtCancel">CANCEL</button>'+
    (t&&t.builtin?'<button class="btn" data-a="rtReset"'+(t.edited?'':' disabled')+'>RESET TO DEFAULT</button>':'')+
    (t&&!t.builtin?(rtConfirm===t.id?'<span class="c11" style="display:flex;align-items:center;gap:8px">DELETE THIS TYPE?<button class="btn sm k" data-a="rtDelYes">DELETE</button><button class="btn sm" data-a="rtDelNo">CANCEL</button></span>':'<button class="btn" data-a="rtDel">DELETE</button>'):'')+'</div></div></div>'};
  RT.types.forEach(t=>{const open=rtOpen===t.id;
    h+='<div style="border-bottom:1px solid var(--ink)"><div class="row" style="border:0"><div style="flex:1 1 300px;min-width:0"><div class="rt">'+esc(t.name)+'</div><div class="rs">'+(t.builtin?(t.edited?'BUILT IN · EDITED':'BUILT IN'):'YOUR OWN')+(RT.auto===t.id?' · CREATED AUTOMATICALLY':'')+'</div>'+
      (open?'':'<div class="c11" style="margin-top:8px;opacity:.75;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">'+esc(t.prompt.replace(/\s+/g,' ').slice(0,140))+'</div>')+'</div>'+
      (open?'':'<button class="btn sm" data-a="rtEdit" data-id="'+esc(t.id)+'">EDIT</button>')+'</div>'+(open?editor(t,false):'')+'</div>'});
  if(rtOpen==='new')h+='<div style="border-bottom:1px solid var(--ink);padding-top:16px"><div class="rt" style="margin-bottom:14px">New report type</div>'+editor(null,true)+'</div>';
  else h+='<div style="padding-top:16px"><button class="btn" data-a="rtNew">+ ADD REPORT TYPE</button></div>';
  h+='<div class="sec">AFTER TRANSCRIBING</div><div class="row"><div style="flex:1 1 260px"><div class="rt">Create automatically</div><div class="rs">WRITTEN RIGHT AFTER EVERY NEW TRANSCRIPT</div></div><select class="in" data-in="rauto" style="flex:0 1 260px"><option value="">Nothing</option>'+RT.types.map(t=>'<option value="'+esc(t.id)+'"'+(RT.auto===t.id?' selected':'')+'>'+esc(t.name)+'</option>').join('')+'</select></div>';
  h+='<div class="sec">AI FOR REPORTS AND SUMMARIES</div><div class="c11" style="padding-top:14px;line-height:1.6">'+(ai.ready?'USING '+esc(ai.modelName)+' AT '+esc(ai.root):'NOT SET UP · ADD AN API KEY OR AN AI SERVER')+'</div>'+
    '<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:20px 28px;padding-top:18px">'+
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px">SERVER · OPTIONAL<input class="in" data-in="aiUrl" value="'+esc(ai.url)+'" placeholder="https://api.openai.com/v1" spellcheck="false"></label>'+
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px">MODEL · OPTIONAL<input class="in" data-in="aiModel" value="'+esc(ai.model)+'" placeholder="gpt-4o-mini" spellcheck="false"></label>'+
    '<label class="c11" style="display:flex;flex-direction:column;gap:8px;grid-column:1/-1">API KEY'+(ai.keySet?' · SAVED ('+esc(ai.keyHint)+')':!ai.url&&ai.whKey?' · USES YOUR OPENAI WHISPER KEY':'')+'<span style="display:flex;gap:8px"><input class="in" style="flex:1" data-in="aiKey" type="'+(showKey?'text':'password')+'" placeholder="'+(ai.keySet?'Type a new key to replace it':'Only needed for a different key or server')+'" autocomplete="off"><button class="btn" data-a="showKey">'+(showKey?'HIDE':'SHOW')+'</button>'+(ai.keySet?'<button class="btn" data-a="aiKeyClear">REMOVE</button>':'')+'</span></label></div>'+
    '<div class="c11" style="margin-top:14px;line-height:1.6">ANY OPENAI-COMPATIBLE CHAT API WORKS, FOR EXAMPLE OLLAMA AT HTTP://192.168.1.20:11434/V1.</div>';
  return h+'</div>'}
async function rtCall(b,msg){try{const r=await api('/api/reports/type',b);if(msg)toast(msg);return r}catch(e){toast(e.message);throw e}}

// live player state, updated without re-rendering
function live(){const r=rec();if(!r||!$('#ppos'))return;let pos=0,playing=false;
  if(playOn==='browser'){if(au.dataset.id==r.id){pos=au.currentTime;playing=!au.paused}}else if(S.play.id===r.id){pos=S.play.pos;playing=S.play.active&&S.play.playing}
  $('#ppos').textContent=fmt(pos);$('#pbar').style.width=Math.min(100,pos/Math.max(1,r.dur)*100)+'%';$('#pbtn').textContent=playing?'❚❚':'▶'}
setInterval(live,500);au.onplay=au.onpause=live;
function ensureAudio(r){if(au.dataset.id!=r.id){au.src='/api/rec/'+r.id+'/audio';au.dataset.id=r.id}}
function seekTo(t){const r=rec();if(!r)return;t=Math.max(0,Math.min(r.dur,t));if(playOn==='browser'){ensureAudio(r);au.currentTime=t;live()}else act('/api/play',{id:r.id,action:S.play.active&&S.play.id===r.id?'seek':'start',pos:t})}

document.addEventListener('click',e=>{const b=e.target.closest('[data-a]');if(!b)return;const a=b.dataset.a,r=rec(),w=S&&S.wifi;
  switch(a){
  case 'retry':poll();break;
  case 'go':go(b.dataset.v);break;
  case 'tabT':tab='transcript';render(true);break;
  case 'unjoin':joinFor=null;render(true);break;
  case 'sel':selId=+b.dataset.id;confirmDel=false;tab='transcript';if(!au.paused)au.pause();render(true);break;
  case 'tab':tab=b.dataset.v;render(true);break;
  case 'playOn':playOn=b.dataset.v;if(playOn==='device')au.pause();else if(S.play.active)act('/api/play',{action:'stop'});render(true);break;
  case 'play':if(!r)break;if(playOn==='browser'){ensureAudio(r);au.paused?au.play():au.pause()}else act('/api/play',{id:r.id,action:'toggle'});break;
  case 'seek':{const cur=playOn==='browser'?(au.dataset.id==r.id?au.currentTime:0):(S.play.id===r.id?S.play.pos:0);seekTo(cur+(+b.dataset.d));break}
  case 'seekTo':seekTo(+b.dataset.t);break;
  case 'scrub':{const bx=b.getBoundingClientRect();seekTo(Math.round((e.clientX-bx.left)/bx.width*r.dur));break}
  case 'send':act('/api/rec/'+r.id+'/send',{});break;
  case 'askdel':confirmDel=true;render(true);break;
  case 'nodel':confirmDel=false;render(true);break;
  case 'del':confirmDel=false;au.pause();au.removeAttribute('src');au.dataset.id='';act('/api/rec/'+r.id+'/delete',{},'RECORDING DELETED');break;
  case 'rec':act('/api/record',{action:b.dataset.v},b.dataset.v==='stop'?'SAVED':b.dataset.v==='mark'?'MARK ADDED':null).then(()=>{if(b.dataset.v==='stop'&&S.recs[0]){selId=S.recs[0].id;go('recordings')}});break;
  case 'wifi':act('/api/wifi',{action:b.dataset.v,i:+(b.dataset.i??-1),dir:+(b.dataset.d||0)},{toggle:w.on?'WI-FI OFF':'WI-FI ON',forget:'NETWORK FORGOTTEN',disconnect:'DISCONNECTED'}[b.dataset.v]);break;
  case 'pick':{const n=w.scan.find(x=>x.ssid===b.dataset.s);if(!n)break;if(n.saved){const i=w.nets.findIndex(x=>x.ssid===n.ssid);act('/api/wifi',{action:'connect',i})}else if(n.sec==='OPEN')act('/api/wifi',{action:'join',ssid:n.ssid,sec:'OPEN'});else{joinFor=joinFor===n.ssid?null:n.ssid;render(true);setTimeout(()=>$('#jpw')&&$('#jpw').focus(),0)}break}
  case 'showPw':{const v=$('#jpw').value;showPw=!showPw;render(true);$('#jpw').value=v;break}
  case 'join':{const pw=$('#jpw').value;if(pw.length<8){toast('AT LEAST 8 CHARACTERS');break}joinFor=null;act('/api/wifi',{action:'join',ssid:b.dataset.s,pw,sec:b.dataset.e});break}
  case 'hidJoin':{const s=$('#hs').value.trim(),p=$('#hp').value;if(!s){toast('ENTER A NETWORK NAME');break}act('/api/wifi',{action:'join',ssid:s,pw:p,sec:p?'WPA2':'OPEN'});break}
  case 'hsAsk':hsConfirm=true;render(true);break;
  case 'hsCancel':hsConfirm=false;render(true);break;
  case 'hsOff':hsConfirm=false;act('/api/hotspot',{on:false},'LOCAL NETWORK OFF');break;
  case 'hsOn':act('/api/hotspot',{on:true},'LOCAL NETWORK ON');break;
  case 'hsShow':hsShow=!hsShow;render(true);break;
  case 'hsGen':{const W=['kestrel','harbor','linden','quartz','meadow','cobalt','fennel','orbit'];hsDraft.pw=W[Math.floor(Math.random()*8)]+'-'+(10+Math.floor(Math.random()*90));hsShow=true;render(true);break}
  case 'hsApply':{const hs=S.hs;if(hsDraft.name===hs.name&&hsDraft.pw===hs.pw)break;if(hsDraft.pw.length<8){toast('AT LEAST 8 CHARACTERS');break}act('/api/hotspot',{name:hsDraft.name,pw:hsDraft.pw},'LOCAL NETWORK UPDATED').then(()=>hsDraft=null);break}
  case 'svc':act('/api/whisper',{svc:+b.dataset.v});break;
  case 'showKey':showKey=!showKey;render(true);break;
  case 'test':act('/api/whisper/test',{});break;
  case 'auto':act('/api/device',{auto:!S.dev.auto});break;
  case 'full':act('/api/device',{fullEvery:Math.max(5,Math.min(200,S.dev.fullEvery+(+b.dataset.d)))});break;
  case 'clear':act('/api/device/clear',{},'SCREEN CLEARED ON THE DEVICE');break;
  case 'otaCheck':act('/api/ota/check',{});break;
  case 'otaInstall':if(confirm('Install '+S.ota.latest+'? The device restarts afterwards.'))act('/api/ota/install',{},'UPDATE STARTED');break;
  case 'restart':act('/api/device/restart',{},'DEVICE RESTARTING · BACK IN ABOUT 10 S');break;
  case 'rtEdit':{const t=RT.types.find(x=>x.id===b.dataset.id);rtOpen=t.id;rtDraft={name:t.name,prompt:t.prompt};rtConfirm=null;render(true);break}
  case 'rtNew':rtOpen='new';rtDraft={name:'',prompt:NEW_PROMPT};rtConfirm=null;render(true);break;
  case 'rtCancel':rtOpen=null;rtDraft=null;rtConfirm=null;render(true);break;
  case 'rtSave':{const id=rtOpen==='new'?'':rtOpen;rtCall({action:'save',id,name:rtDraft.name,prompt:rtDraft.prompt},'SAVED').then(()=>{rtOpen=null;rtDraft=null;RT=null;poll()}).catch(()=>{});break}
  case 'rtReset':rtCall({action:'reset',id:rtOpen},'DEFAULT PROMPT RESTORED').then(()=>{const id=rtOpen;RT=null;api('/api/reports').then(d=>{RT=d;const t=d.types.find(x=>x.id===id);if(t)rtDraft={name:t.name,prompt:t.prompt};render(true)})}).catch(()=>{});break;
  case 'rtDel':rtConfirm=rtOpen;render(true);break;
  case 'rtDelNo':rtConfirm=null;render(true);break;
  case 'rtDelYes':rtCall({action:'delete',id:rtOpen},'REPORT TYPE DELETED').then(()=>{rtOpen=null;rtDraft=null;rtConfirm=null;RT=null;poll()}).catch(()=>{});break;
  case 'aiKeyClear':api('/api/reports/settings',{key:''}).then(()=>{toast('KEY REMOVED');RT=null;poll()}).catch(e=>toast(e.message));break;
  case 'repMake':if(!r)break;act('/api/rec/'+r.id+'/report',{type:repSel});break;
  case 'repMakeType':if(!r)break;tab='reports';repSel=b.dataset.t;act('/api/rec/'+r.id+'/report',{type:b.dataset.t});break;
  case 'repDel':repConfirm=b.dataset.t;render(true);break;
  case 'repDelNo':repConfirm=null;render(true);break;
  case 'repDelYes':repConfirm=null;act('/api/rec/'+r.id+'/report/delete',{type:b.dataset.t},'REPORT DELETED');break;
  case 'repCopy':{const x=((det&&det.reports)||[]).find(v=>v.type===b.dataset.t);if(!x)break;(navigator.clipboard?navigator.clipboard.writeText(x.text):Promise.reject()).then(()=>toast('COPIED')).catch(()=>{const ta=document.createElement('textarea');ta.value=x.text;document.body.appendChild(ta);ta.select();document.execCommand('copy');ta.remove();toast('COPIED')});break}
  case 'spkName':{if(!r||!det||!det.names)break;const n=+b.dataset.n,v=prompt('Name for '+spkN(n)+' (leave empty for Speaker '+n+')',det.names[n-1]||'');if(v===null)break;
    api('/api/rec/'+r.id+'/speaker',{n,name:v.trim()}).then(()=>{det.names[n-1]=v.trim().slice(0,40);toast(v.trim()?'SPEAKER NAMED':'NAME REMOVED');render(true)}).catch(e=>toast(e.message));break}
  case 'srvKeyClear':act('/api/whisper',{srvKey:''},'API KEY REMOVED');break;
  case 'phoneSend':{const v=$('#pv').value.trim();api('/api/setup',{value:v}).then(()=>{phoneSent=true;render(true)}).catch(e=>toast(e.message));break}
  }});
function otaRows(){const o=S.ota||{};
  if(o.installing)return '<div class="row"><div style="flex:1 1 260px"><div class="rt">Updating to '+esc(o.latest)+'</div><div class="rs">DOWNLOADING FROM GITHUB · '+o.pct+'% · THE DEVICE RESTARTS BY ITSELF</div></div><span style="width:160px;height:14px;border:2px solid var(--ink);padding:2px;display:flex"><i style="width:'+o.pct+'%;background:var(--ink);display:block"></i></span></div>';
  if(o.avail)return '<div class="row"><div style="flex:1 1 260px"><div class="rt">Update '+esc(o.latest)+' available</div><div class="rs">NOW '+esc(S.fw)+(o.notes?' · '+esc(o.notes.toUpperCase()):'')+'</div></div><button class="btn sm k" data-a="otaInstall">INSTALL</button></div>';
  const when=o.checked?dstr(o.checked):'';
  return '<div class="row"><div style="flex:1 1 260px"><div class="rt">Updates</div><div class="rs">'+(o.checking?'CHECKING GITHUB'+dots():o.err?esc(o.err):o.checked?'UP TO DATE · CHECKED '+when:'CHECKS GITHUB EVERY 6 HOURS WHILE ONLINE')+' · '+esc(o.repo||'')+'</div></div><button class="btn sm" data-a="otaCheck">CHECK NOW</button></div>'}
function fwUpload(f){if(!f)return;if(!/\.bin$/i.test(f.name)){toast('CHOOSE A .BIN FILE');return}
  if(!confirm('Install '+f.name+' ('+(f.size/1e6).toFixed(2)+' MB)? The device restarts afterwards.'))return;
  const x=new XMLHttpRequest(),fd=new FormData();fd.append('fw',f,f.name);fwUp=0;render(true);
  x.upload.onprogress=e=>{if(e.lengthComputable){fwUp=Math.min(99,Math.round(e.loaded/e.total*100));render(true)}};
  x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(_){}if(x.status===200){fwUp='done';render(true);toast('FIRMWARE UPDATED · RESTARTING');setTimeout(()=>{fwUp=null;poll()},15000)}else{fwUp=null;render(true);toast(j.error||'UPDATE FAILED')}};
  x.onerror=()=>{fwUp=null;render(true);toast('CONNECTION LOST DURING UPLOAD')};
  x.open('POST','/api/ota');x.send(fd)}
document.addEventListener('change',e=>{const el=e.target.closest('[data-in]');if(!el)return;const k=el.dataset.in,v=el.value,r=rec();
  if(k==='fw'){fwUpload(el.files[0]);return}
  if(k==='title'&&r&&v.trim())act('/api/rec/'+r.id+'/rename',{title:v.trim()});
  else if(k==='key'&&v.trim())act('/api/whisper',{key:v.trim()},'API KEY SAVED').then(()=>el.value='');
  else if(k==='modelName')act('/api/whisper',{modelName:v.trim()},v.trim()?'MODEL SAVED':'DEFAULT MODEL');
  else if(k==='miKey'&&v.trim())act('/api/whisper',{miKey:v.trim()},'API KEY SAVED').then(()=>el.value='');
  else if(k==='spk')act('/api/whisper',{spk:v==='1'},v==='1'?'SPEAKERS ARE LABELLED':'NO SPEAKER LABELS');
  else if(k==='srvKey'&&v.trim())act('/api/whisper',{srvKey:v.trim()},'API KEY SAVED').then(()=>el.value='');
  else if(k==='server')act('/api/whisper',{server:v.trim()},'SERVER SAVED');
  else if(k==='model'||k==='lang')act('/api/whisper',{[k]:+v});
  else if(k==='vol')act('/api/device',{vol:+v});
  else if(k==='sleepMin')act('/api/device',{sleepMin:+v},+v?'SLEEPS AFTER '+v+' MIN':'NEVER SLEEPS');
  else if(k==='repSel'){repSel=v;render(true)}
  else if(k==='recType'&&r)act('/api/rec/'+r.id+'/rtype',{type:v},v===''?'USES THE DEFAULT':v==='none'?'NO REPORT':'REPORT TYPE: '+rtName(v).toUpperCase()).then(()=>{repSel=null;render(true)});
  else if(k==='rauto')api('/api/reports/settings',{auto:v}).then(()=>{toast(v?'CREATED AUTOMATICALLY':'NOTHING AUTOMATIC');RT=null;poll()}).catch(e=>toast(e.message));
  else if(k==='aiUrl'||k==='aiModel'||(k==='aiKey'&&v.trim()))api('/api/reports/settings',{[k==='aiUrl'?'url':k==='aiModel'?'model':'key']:v.trim()}).then(()=>{toast('SAVED');if(k==='aiKey')el.value='';RT=null;poll()}).catch(e=>toast(e.message));});
document.addEventListener('input',e=>{const el=e.target.closest('[data-in]');if(!el)return;if(rtDraft){if(el.dataset.in==='rtName')rtDraft.name=el.value;if(el.dataset.in==='rtPrompt')rtDraft.prompt=el.value}if(!hsDraft)return;if(el.dataset.in==='hsName')hsDraft.name=el.value;if(el.dataset.in==='hsPw')hsDraft.pw=el.value});
document.addEventListener('keydown',e=>{if(e.key==='Enter'&&e.target.matches('input')){const t=e.target;if(t.id==='jpw')$('[data-a="join"]').click();else if(t.id==='pv')$('[data-a="phoneSend"]').click();else t.blur()}});
document.addEventListener('visibilitychange',()=>{if(document.visibilityState==='visible')poll()});
window.addEventListener('hashchange',()=>{const v=location.hash.slice(1);if(v&&v!==view){view=v;render(true)}});
poll();
</script></body></html>)HTML";
