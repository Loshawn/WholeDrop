'use strict';
const $=s=>document.querySelector(s), ui={ready:false,busy:false,file:null,files:[],xhr:null,entries:[],lastBytes:0,lastTick:0};
let petReactUntil=0,petPendingMood=null;
const characterVersion='__CHARACTER_VERSION__';
const speech={boot:'等我收拾一下仓库…',idle:'有文件？给我康康。',uploading:'收到，正在认真干活！',downloading:'给，接好你的文件。',happy:'传好啦，开饭！',error:'等一下，我修修就好。'};
function mood(state){if(performance.now()<petReactUntil){petPendingMood=state;return}let character=$('#whale');character.dataset.mood=state;$('#speech').textContent=speech[state]||speech.idle;let source='/assets/whale-'+state+'.webp?v='+characterVersion;if(character.getAttribute('src')===source)return;let next=new Image();next.onload=()=>{if(character.dataset.mood===state)character.src=source};next.onerror=()=>{setTimeout(()=>{if(character.dataset.mood===state)mood(state)},2000)};next.src=source}
function message(text,state=''){$('#message').textContent=text;$('#message').className=state}
function formatBytes(n){if(n<1000)return n+' B';if(n<1e6)return(n/1e3).toFixed(1)+' KB';if(n<1e9)return(n/1e6).toFixed(2)+' MB';return(n/1e9).toFixed(2)+' GB'}
function showSpeed(bytes,seconds,label){const speed=bytes/Math.max(seconds,.001)/1e6;$('#speedLabel').textContent=label;$('#speedValue').replaceChildren(document.createTextNode(speed.toFixed(2)+' '));let unit=document.createElement('em');unit.textContent='MB/s';$('#speedValue').append(unit);$('#speedSub').textContent=(speed*8).toFixed(2)+' Mbps · '+seconds.toFixed(1)+' 秒'}
function addHistory(text){let time=new Date().toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit'});ui.entries.unshift({time,text});ui.entries=ui.entries.slice(0,5);$('#history').replaceChildren(...ui.entries.map(e=>{let li=document.createElement('li'),t=document.createElement('time'),s=document.createElement('span');t.textContent=e.time;s.textContent=e.text;li.append(t,s);return li}))}
function updateButton(){$('#uploadButton').disabled=!ui.ready||ui.busy||!ui.file;$('#file').disabled=ui.busy;$('#refresh').disabled=ui.busy;$('#cancel').hidden=!ui.busy}
function setBusy(b){ui.busy=b;updateButton()}
function selectFile(f){if(!f||ui.busy)return;if(f.size>1024**3){message('这个文件超过 1 GiB，换一个小一点的吧。','error');return}ui.file=f;$('#fileLabel').textContent=f.name;$('#fileMeta').textContent=formatBytes(f.size)+' · 已选好，准备投喂';$('#progress').value=0;$('#percent').textContent='0%';$('#transferLabel').textContent='准备投喂';message('文件已选好。确认存储就绪后，点击「开始投喂」。');updateButton()}
$('#file').addEventListener('change',e=>selectFile(e.target.files[0]));
for(const event of ['dragenter','dragover'])$('#dropZone').addEventListener(event,e=>{e.preventDefault();if(!ui.busy)$('#dropZone').classList.add('dragover')});
for(const event of ['dragleave','drop'])$('#dropZone').addEventListener(event,e=>{e.preventDefault();$('#dropZone').classList.remove('dragover');if(event==='drop')selectFile(e.dataTransfer.files[0])});
function renderFiles(){let query=$('#search').value.trim().toLocaleLowerCase(),filtered=ui.files.filter(f=>f.name.toLocaleLowerCase().includes(query));$('#fileCount').textContent=ui.files.length;$('#files').replaceChildren();$('#empty').hidden=filtered.length>0;$('#empty strong').textContent=query?'没有找到这个文件':'小仓库还空着呢';$('#empty p').textContent=query?'试试短一点的关键词。':'第一份文件，就交给你啦。';for(const f of filtered){let li=document.createElement('li');li.className='file-row';let glyph=document.createElement('span');glyph.className='file-glyph';glyph.textContent=(f.name.includes('.')?f.name.split('.').pop():'FILE').slice(0,4).toUpperCase();let info=document.createElement('div');info.className='file-info';let name=document.createElement('span'),size=document.createElement('span');name.className='file-name';name.textContent=f.name;name.title=f.name;size.className='file-size';size.textContent=formatBytes(f.bytes);info.append(name,size);let actions=document.createElement('div');actions.className='file-actions';let link=document.createElement('button');link.type='button';link.className='download-link';link.textContent='↓ 下载';link.disabled=ui.busy;link.setAttribute('aria-label','下载 '+f.name);link.addEventListener('click',()=>browserDownload(f));actions.append(link);li.append(glyph,info,actions);$('#files').append(li)}}
async function getJson(url){let r=await fetch(url,{cache:'no-store'});if(!r.ok){let text=await r.text();throw new Error(text||'设备暂时忙，请稍后重试')}return r.json()}
async function readStatus(){
 try{
  let s=await getJson('/api/status');ui.ready=s.sd_ready;$('#testToggle').hidden=!(s.features&&s.features.speed_test);$('.benchmark').hidden=$('#testToggle').hidden;
  $('#clients').textContent=s.clients;$('#connectionDot').className='online';$('#connectionLabel').textContent='设备已连接';
  $('#storageLabel').textContent=s.sd_ready?'TF 卡已就绪':s.storage_state==='initializing'?'小仓库整理中':'存储暂不可用';
  if(s.usage_known&&s.volume_bytes>0){
   const total=s.volume_bytes,used=s.used_bytes,free=s.free_bytes,percent=Math.min(100,Math.max(0,used/total*100));
   $('#storageSub').textContent='已用 '+formatStorage(used)+' / 共 '+formatStorage(total);
   $('#storageFree').textContent=(s.sd_ready?'':'上次统计 · ')+'剩余 '+formatStorage(free)+' · '+percent.toFixed(2)+'% 已用';
   $('#storageFill').style.width=percent+'%';$('#storageMeter').setAttribute('aria-valuenow',percent.toFixed(2));
  }else{$('#storageSub').textContent=s.sd_ready?'正在统计卡片空间':'存储正在自动恢复';$('#storageFree').textContent='空间统计就绪后会自动显示';}
  $('#retry').hidden=s.sd_ready||s.storage_state==='initializing';
  if(!ui.busy){const t=s.transfer;if(t&&t.active)mood(t.direction==='upload'?'uploading':'downloading');else if(!s.sd_ready)mood(s.storage_state==='initializing'?'boot':'error');else if(!$('#whale').dataset.hold)mood('idle')}
  updateButton();return s;
 }catch(e){$('#connectionDot').className='';$('#connectionLabel').textContent='等待设备重连';ui.ready=false;updateButton();if(!ui.busy){mood('boot');$('#speech').textContent='我在这里，等你连回来。'}return null}
}
function formatStorage(n){return n>=1073741824?(n/1073741824).toFixed(2)+' GiB':n>=1048576?(n/1048576).toFixed(1)+' MiB':(n/1024).toFixed(1)+' KiB'}
async function refresh(){if(ui.busy)return;let s=await readStatus();if(!s)return;if(!s.sd_ready){$('#listHint').textContent='存储恢复后，文件列表会重新出现';return}try{ui.files=await getJson('/api/files');renderFiles();$('#listHint').textContent=ui.files.length+' 份文件 · 仅保存在这台设备中'}catch(e){message('暂时读不到文件列表，设备正在恢复，请稍后刷新。','error')}}
$('#refresh').addEventListener('click',refresh);$('#search').addEventListener('input',renderFiles);
$('#retry').addEventListener('click',async()=>{try{await fetch('/api/storage/retry',{method:'POST'});mood('boot');message('正在重新检测 TF 卡。大肥鱼整理好仓库后会自动恢复。');setTimeout(refresh,6000)}catch(e){message('还没有连上设备，连接热点后再试一下。','error')}});
function responseError(status,text){if(status===409)return'设备正在传输或恢复存储，请稍后重试。';if(status===503)return'存储暂时不可用，设备正在尝试恢复。';if(status===413)return'文件太大了，单个文件最大 1 GiB。';return'文件没有保存成功，请保留原文件，稍后重试。'+(text?'':'')}
$('#form').addEventListener('submit',e=>{e.preventDefault();if(!ui.file||ui.busy||!ui.ready)return;let f=ui.file,x=new XMLHttpRequest(),start=performance.now(),settled=false;ui.xhr=x;setBusy(true);renderFiles();mood('uploading');message('大肥鱼正在接收文件…');$('#transferLabel').textContent='正在投喂';$('#progress').value=0;x.open('POST','http://'+location.hostname+':81/upload?name='+encodeURIComponent(f.name));x.setRequestHeader('Content-Type','application/octet-stream');x.timeout=0;x.upload.onprogress=e=>{if(!e.lengthComputable)return;let percent=100*e.loaded/e.total,t=(performance.now()-start)/1000;$('#progress').value=percent;$('#percent').textContent=percent.toFixed(0)+'%';$('#transferRate').textContent=formatBytes(e.loaded)+' / '+formatBytes(e.total)+' · '+(e.loaded/Math.max(t,.001)/1e6).toFixed(2)+' MB/s';showSpeed(e.loaded,t,'正在接收');if(e.loaded===e.total){$('#transferLabel').textContent='正在确认保存';message('文件已经发送完，正在写入存储并核对文件列表，请稍候。')}};const finish=()=>{if(settled)return;settled=true;ui.xhr=null;setBusy(false);renderFiles()};x.onload=async()=>{let t=(performance.now()-start)/1000;if(x.status!==200){finish();mood('error');message(responseError(x.status,x.responseText),'error');addHistory('上传未完成 · '+f.name);await readStatus();return}try{let receipt=JSON.parse(x.responseText),savedName=receipt.name;let list=await getJson('/api/files');if(!savedName||!list.some(v=>v.name===savedName&&v.bytes===f.size))throw new Error('目录未确认保存，请保留原文件。');ui.files=list;finish();showSpeed(f.size,t,'刚刚上传');$('#transferLabel').textContent='投喂完成，已确认保存';$('#percent').textContent='100%';$('#progress').value=100;$('#transferRate').textContent=formatBytes(f.size)+' · '+t.toFixed(1)+' 秒';message('上传已确认：'+savedName+' 已在小仓库里，其他设备可以下载了。','success');mood('happy');$('#whale').dataset.hold='yes';setTimeout(()=>{delete $('#whale').dataset.hold;readStatus()},5000);addHistory('已保存 · '+f.name+' · '+(f.size/t/1e6).toFixed(2)+' MB/s');renderFiles();$('#listHint').textContent=ui.files.length+' 份文件 · 仅保存在这台设备中'}catch(error){finish();mood('error');message(error.message,'error')}};x.onerror=()=>{finish();mood('error');message('连接中断。重新连上热点后，刷新列表确认文件是否保存。','error')};x.onabort=()=>{finish();mood('idle');$('#transferLabel').textContent='已取消';message('这次投喂已取消。原文件仍在你的设备中。');addHistory('取消上传 · '+f.name)};x.send(f)});
$('#cancel').addEventListener('click',()=>{if(ui.testing){stopTest=true;$('#testState').textContent='完成当前文件校验和清理后停止';return}if(ui.xhr)ui.xhr.abort();if(ui.downloadController)ui.downloadController.abort()});
async function speedTest(f){if(ui.busy)return;let controller=new AbortController();ui.downloadController=controller;setBusy(true);renderFiles();mood('downloading');let start=performance.now(),bytes=0,lastPaint=0;message('正在下载测速，数据不会保存到本机。');try{let r=await fetch('/content',{method:'POST',body:new URLSearchParams({name:f.name}),cache:'no-store',signal:controller.signal});if(!r.ok)throw new Error('设备正忙，稍后再试');let reader=r.body.getReader();while(true){let chunk=await reader.read();if(chunk.done)break;bytes+=chunk.value.length;let now=performance.now();if(now-lastPaint>250){showSpeed(bytes,(now-start)/1000,'正在送出');message('下载测速 · '+formatBytes(bytes)+' / '+formatBytes(f.bytes));lastPaint=now}}if(bytes!==f.bytes)throw new Error('接收长度与文件不一致，请重新测试');let seconds=(performance.now()-start)/1000;showSpeed(bytes,seconds,'刚刚下载');message('下载测速完成：'+(bytes/seconds/1e6).toFixed(2)+' MB/s · '+(bytes*8/seconds/1e6).toFixed(2)+' Mbps。测速未保存文件；长度检查不代表内容校验。','success');addHistory('下载测速 · '+f.name+' · '+(bytes/seconds/1e6).toFixed(2)+' MB/s');mood('happy')}catch(e){message(e.name==='AbortError'?'下载测速已取消。':e.message,e.name==='AbortError'?'':'error');mood(e.name==='AbortError'?'idle':'error')}finally{ui.downloadController=null;setBusy(false);renderFiles()}}
$('#copyLink').addEventListener('click',async()=>{let text=location.origin+'/';try{if(navigator.clipboard&&window.isSecureContext)await navigator.clipboard.writeText(text);else{let t=document.createElement('textarea');t.value=text;document.body.append(t);t.select();document.execCommand('copy');t.remove()}$('#toast').textContent='地址已复制，记得先连接同一个热点';$('#toast').className='visible';setTimeout(()=>$('#toast').className='',2600)}catch(e){message('分享地址：'+text+'，请让对方先连接 whaledrop。')}});
mood('boot');refresh();setInterval(()=>{if(!document.hidden)readStatus()},2500);setInterval(()=>{if(!ui.busy&&!document.hidden)refresh()},15000);

async function browserDownload(f){
 if(ui.busy)return;
 if(f.bytes>64*1024*1024){if(confirm('大文件将交给浏览器保存，可能由 IDM 接管。继续下载？')){let a=document.createElement('a');a.href='/download?name='+encodeURIComponent(f.name);a.download=f.name;a.click()}return}
 let controller=new AbortController();ui.downloadController=controller;setBusy(true);renderFiles();mood('downloading');let start=performance.now(),bytes=0,lastPaint=0;
 message('正在浏览器内接收文件，完成后交给浏览器保存。');
 $('#progress').value=0;$('#progress').setAttribute('aria-label','下载进度');$('#percent').textContent='0%';$('#transferLabel').textContent='正在下载 · '+f.name;$('#transferRate').textContent='等待设备发送文件';
 const paintDownload=()=>{let seconds=(performance.now()-start)/1000,percent=f.bytes?Math.min(100,100*bytes/f.bytes):100;$('#progress').value=percent;$('#percent').textContent=percent.toFixed(0)+'%';$('#transferRate').textContent=formatBytes(bytes)+' / '+formatBytes(f.bytes)+' · '+(bytes/Math.max(seconds,.001)/1e6).toFixed(2)+' MB/s';showSpeed(bytes,seconds,'正在下载')};
 try{
  let r=await fetch('/content',{method:'POST',body:new URLSearchParams({name:f.name}),cache:'no-store',signal:controller.signal});
  if(!r.ok)throw Error('设备正忙或存储暂不可用，请稍后重试');
  let reader=r.body.getReader(),parts=[];
  while(true){let item=await reader.read();if(item.done)break;parts.push(item.value);bytes+=item.value.length;let now=performance.now();if(now-lastPaint>250){paintDownload();lastPaint=now}}
  if(bytes!==f.bytes)throw Error('文件没有完整接收，请重试');
  let blob=new Blob(parts,{type:'application/octet-stream'}),url=URL.createObjectURL(blob),anchor=document.createElement('a');anchor.href=url;anchor.download=f.name;document.body.append(anchor);anchor.click();anchor.remove();setTimeout(()=>URL.revokeObjectURL(url),60000);
  paintDownload();$('#transferLabel').textContent='下载完成，已交给浏览器保存';showSpeed(bytes,(performance.now()-start)/1000,'刚刚下载');message('文件已完整接收，已交给浏览器保存：'+f.name,'success');mood('happy');addHistory('浏览器下载 · '+f.name);
 }catch(e){$('#transferLabel').textContent=e.name==='AbortError'?'下载已取消':'下载未完成';message(e.name==='AbortError'?'下载已取消。':e.message,e.name==='AbortError'?'':'error');mood(e.name==='AbortError'?'idle':'error')}
 finally{ui.downloadController=null;setBusy(false);renderFiles()}
}

(function setupCompanion(){
 const pet=$('#pet');let drag=null,lastReaction=-1,reactionTimer;
 const reactions=[
  {mood:'petted',label:'摸摸头',kind:'petting',particles:'♥',lines:['嗯……这个力度刚刚好。','小鲸充电中，再摸一下嘛。','今天的辛苦，被你摸走啦。']},
  {mood:'shy',label:'害羞',kind:'bashful',particles:'♡',lines:['突、突然这么温柔，我会不好意思的。','脸红是因为热点太热，才不是因为你！','别看我啦……也不是不让你看。']},
  {mood:'pout',label:'鼓脸',kind:'pouting',particles:'!',lines:['不是大肥鱼！是小鲸！','再揉就要收一碗白饭的费用啦！','哼！头发都被你揉乱了……']},
  {mood:'sleepy',label:'困困',kind:'sleepy',particles:'☾',lines:['呼啊……没有文件的话，借我眯一小会儿。','抱着小枕头，也会帮你看好仓库的。','小鲸进入低功耗撒娇模式……']},
  {mood:'surprised',label:'惊喜',kind:'surprised',particles:'✧',lines:['哇！是专门来找我玩的吗！','发现了！今天的小幸运是你！','诶？还有奖励？我已经准备好啦！']},
  {mood:'smug',label:'得意',kind:'smug',particles:'✦',lines:['哼哼，能干的小鲸就是我！','这点小事，交给本鲸鱼就好。','摸头奖励收到！允许你再夸我三句。']},
  {mood:'happy',label:'开心',kind:'delighted',particles:'♥',lines:['好感度 +1！今天也会努力营业！','给你一个小小的鲸鱼祝福！','被你找到啦！来，击个掌。']},
  {mood:'idle',label:'馋馋',kind:'hungry',particles:'✦',lines:['文件我来保管，白饭你来保管……不，我也来！','摸头能兑换一碗白饭吗？','今天的工作餐，可以再添一碗吗？']}
 ];
 let bag=[],baselineMood='idle',lastLine={};
 function pickReaction(){
  if(!bag.length){bag=reactions.map((_,i)=>i);for(let i=bag.length-1;i>0;i--){let j=Math.floor(Math.random()*(i+1));[bag[i],bag[j]]=[bag[j],bag[i]]}if(bag[bag.length-1]===lastReaction)[bag[0],bag[bag.length-1]]=[bag[bag.length-1],bag[0]];}
  let index=bag.pop();lastReaction=index;let item=reactions[index],line;
  do{line=Math.floor(Math.random()*item.lines.length)}while(line===lastLine[index]);lastLine[index]=line;
  return {...item,text:item.lines[line]};
 }
 function react(){
  clearTimeout(reactionTimer);let reaction=pickReaction();
  if(performance.now()>=petReactUntil)baselineMood=$('#whale').dataset.mood||'idle';
  let queued=petPendingMood;petReactUntil=0;mood(reaction.mood);petPendingMood=queued;
  petReactUntil=performance.now()+4200;pet.dataset.reaction=reaction.kind;pet.dataset.emotion=reaction.mood;
  $('#speech').textContent=reaction.label+' · '+reaction.text;
  pet.classList.remove('is-petted');void pet.offsetWidth;pet.classList.add('is-petted');
  pet.querySelectorAll('.pet-heart').forEach(el=>el.remove());
  for(let i=0;i<4;i++){let heart=document.createElement('span');heart.className='pet-heart';heart.textContent=i%2?'✦':reaction.particles;heart.style.setProperty('--i',i);heart.setAttribute('aria-hidden','true');pet.append(heart)}
  reactionTimer=setTimeout(()=>{petReactUntil=0;pet.classList.remove('is-petted');delete pet.dataset.reaction;delete pet.dataset.emotion;pet.querySelectorAll('.pet-heart').forEach(el=>el.remove());let next=petPendingMood||baselineMood;petPendingMood=null;mood(next)},4250);
 }
 // Preload one sprite at a time while idle; normal transfers get priority.
 let preloadIndex=0;
 function warmReactions(){if(preloadIndex>=6)return;if(ui.busy){setTimeout(warmReactions,2500);return}let image=new Image();image.onload=image.onerror=()=>setTimeout(warmReactions,800);image.src='/assets/whale-'+reactions[preloadIndex++].mood+'.webp?v='+characterVersion;}
 setTimeout(warmReactions,3000);
 function clamp(x,y){pet.style.left=Math.max(0,Math.min(innerWidth-pet.offsetWidth,x))+'px';pet.style.top=Math.max(0,Math.min(innerHeight-pet.offsetHeight,y))+'px';pet.style.right='auto';pet.style.bottom='auto'}
 function restore(){try{let p=JSON.parse(localStorage.getItem('whaledrop-pet'));if(p)clamp(p.x*innerWidth,p.y*innerHeight)}catch(e){}}
 restore();
 pet.addEventListener('pointerdown',e=>{if(e.button!==0)return;let r=pet.getBoundingClientRect();drag={x:e.clientX-r.left,y:e.clientY-r.top,startX:e.clientX,startY:e.clientY,moved:false};pet.setPointerCapture(e.pointerId);e.preventDefault()});
 pet.addEventListener('pointermove',e=>{if(!drag)return;if(Math.hypot(e.clientX-drag.startX,e.clientY-drag.startY)>7)drag.moved=true;if(drag.moved){pet.classList.add('dragging');clamp(e.clientX-drag.x,e.clientY-drag.y)}});
 function end(e){if(!drag)return;let moved=drag.moved;drag=null;pet.classList.remove('dragging');if(e.type==='pointercancel')return;if(moved){let r=pet.getBoundingClientRect();try{localStorage.setItem('whaledrop-pet',JSON.stringify({x:r.left/innerWidth,y:r.top/innerHeight}))}catch(err){}}else react()}
 pet.addEventListener('pointerup',end);pet.addEventListener('pointercancel',end);
 pet.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();react()}});
 addEventListener('resize',()=>{let r=pet.getBoundingClientRect();clamp(r.left,r.top)});
})();
$('#testToggle').onclick=()=>{$('#testPanel').hidden=!$('#testPanel').hidden;if(!$('#testPanel').hidden)$('#testPanel').scrollIntoView({behavior:'smooth',block:'center'})};
let stopTest=false;$('#testStop').onclick=()=>{stopTest=true;$('#testState').textContent='完成当前文件校验和清理后停止'};
$('#testStart').onclick=async()=>{if(ui.busy||!ui.ready)return;let sizes=[...document.querySelectorAll('#testSizes input:checked')].map(x=>+x.value);if(!sizes.length)return;setBusy(true);ui.testing=true;renderFiles();stopTest=false;$('#testStop').hidden=false;$('#testStart').disabled=true;let controller=new AbortController();ui.downloadController=controller;try{for(let size of sizes){if(stopTest)break;let names=[];try{$('#testState').textContent='准备 '+formatBytes(size)+' 测试文件（不计时）';let prep=await fetch('/api/test/prepare',{method:'POST',body:new URLSearchParams({bytes:size}),signal:controller.signal});if(!prep.ok)throw Error(await prep.text());let source=await prep.json();names.push(source.name);mood('downloading');$('#testState').textContent='下载 '+formatBytes(size);let start=performance.now(),r=await fetch('/content',{method:'POST',body:new URLSearchParams({name:source.name}),signal:controller.signal});if(!r.ok)throw Error('下载失败');let blob=await r.blob(),down=(performance.now()-start)/1000;if(blob.size!==size)throw Error('下载长度不符');let digest=await digestBlob(blob);mood('uploading');$('#testState').textContent='原样上传 '+formatBytes(size);start=performance.now();r=await fetch('http://'+location.hostname+':81/upload?name='+encodeURIComponent(source.name),{method:'POST',body:blob,signal:controller.signal});if(!r.ok)throw Error(await r.text());let receipt=await r.json(),up=(performance.now()-start)/1000;names.push(receipt.name);let check=await fetch('/content',{method:'POST',body:new URLSearchParams({name:receipt.name}),signal:controller.signal});if(!check.ok)throw Error('回读失败');let verified=await digestBlob(await check.blob());if(typeof digest==='string'?digest!==verified:digest.length!==verified.length||digest.some((v,i)=>v!==verified[i]))throw Error('回读校验失败');let row=document.createElement('tr');for(let text of [formatBytes(size),(size/down/1e6).toFixed(2),(size/up/1e6).toFixed(2),'一致 · '+($('#testDistance').value||'未标距离')]){let td=document.createElement('td');td.textContent=text;row.append(td)}$('#testResults').append(row);showSpeed(size,up,'测速上传')}finally{for(let name of names)await fetch('/api/lab-file?name='+encodeURIComponent(name),{method:'DELETE'}).catch(()=>{})}}$('#testState').textContent=stopTest?'已停止':'测试完成，临时文件已清理';mood('happy')}catch(e){$('#testState').textContent=e.name==='AbortError'?'测试已取消':e.message;mood('error')}finally{ui.downloadController=null;ui.testing=false;setBusy(false);$('#testStop').hidden=true;$('#testStart').disabled=false;await refresh()}};
// WebCrypto is unavailable on plain HTTP; exact byte comparison uses a retained Blob.
async function digestBlob(blob){let bytes=new Uint8Array(await blob.arrayBuffer());if(globalThis.crypto&&crypto.subtle)return [...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].join(',');return bytes}
