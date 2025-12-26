#ifndef WEB_SCRIPT_H
#define WEB_SCRIPT_H

// JavaScript for ATS-Mini web interface
// Auto-generated from Network.cpp - do not edit directly

static const char WEB_SCRIPT_JS[] PROGMEM = R"rawliteral(
let radioVol=0,radioBrt=128,memData={memories:[],bands:[],modes:[]},optData={};

function cmd(c){
  fetch('/cmd/'+c).then(r=>r.json()).then(d=>{
    if(d.ok)update();
  }).catch(e=>console.error(e));
}

function loadOptions(){
  fetch('/options').then(r=>r.json()).then(d=>{
    optData=d;
    populateSelect('sel-band',d.bands,d.currentBand);
    populateSelect('sel-mode',d.modes,d.currentMode);
    populateSelect('sel-step',d.steps,d.currentStep);
    populateSelect('sel-bw',d.bandwidths,d.currentBandwidth);
    populateAgc(d.agcMax,d.currentAgc);
  }).catch(e=>console.error(e));
}

function populateSelect(id,opts,current){
  let sel=document.getElementById(id);
  sel.innerHTML=opts.map(o=>'<option'+(o===current?' selected':'')+'>'+o+'</option>').join('');
}

function populateAgc(max,current){
  let sel=document.getElementById('sel-agc');
  let opts=[];
  for(let i=0;i<=max;i++)opts.push(i);
  sel.innerHTML=opts.map(v=>'<option'+(v===current?' selected':'')+'>'+v+'</option>').join('');
}

function setBand(v){
  fetch('/set/band?name='+encodeURIComponent(v)).then(r=>r.json()).then(d=>{
    if(d.ok){loadOptions();update();}
    else alert(d.error||'Error');
  }).catch(e=>console.error(e));
}

function setMode(v){
  fetch('/set/mode?name='+encodeURIComponent(v)).then(r=>r.json()).then(d=>{
    if(d.ok){loadOptions();update();}
    else alert(d.error||'Error');
  }).catch(e=>console.error(e));
}

function setStep(v){
  fetch('/set/step?name='+encodeURIComponent(v)).then(r=>r.json()).then(d=>{
    if(d.ok)update();
    else alert(d.error||'Error');
  }).catch(e=>console.error(e));
}

function setBw(v){
  fetch('/set/bandwidth?name='+encodeURIComponent(v)).then(r=>r.json()).then(d=>{
    if(d.ok)update();
    else alert(d.error||'Error');
  }).catch(e=>console.error(e));
}

function setAgc(v){
  fetch('/set/agc?value='+v).then(r=>r.json()).then(d=>{
    if(d.ok)update();
    else alert(d.error||'Error');
  }).catch(e=>console.error(e));
}

let isStandby=false;
function toggleStandby(){
  let c=isStandby?'o':'O';
  fetch('/cmd/'+c).then(r=>r.json()).then(d=>{
    if(d.ok){
      isStandby=!isStandby;
      let btn=document.getElementById('btn-standby');
      btn.textContent=isStandby?'Wake Up':'Standby';
      btn.className=isStandby?'btn btn-sm btn-primary':'btn btn-sm';
    }
  }).catch(e=>console.error(e));
}

function directTune(){
  let val=document.getElementById('freq-input').value;
  let unit=document.getElementById('freq-unit').value;
  let freq=parseFloat(val);
  if(isNaN(freq)||freq<=0){alert('Enter a valid frequency');return;}
  if(unit==='MHz')freq=Math.round(freq*100);
  else freq=Math.round(freq);
  fetch('/tune?freq='+freq).then(r=>r.json()).then(d=>{
    if(d.ok){document.getElementById('freq-input').value='';loadOptions();update();}
    else alert(d.error||'Tune failed');
  }).catch(e=>console.error(e));
}

function setVol(v){
  let tgt=parseInt(v);
  let diff=tgt-radioVol;
  radioVol=tgt;
  sendSteps(diff,'V','v');
}

function setBrt(v){
  let tgt=parseInt(v);
  let diff=tgt-radioBrt;
  radioBrt=tgt;
  sendSteps(diff,'L','l');
}

function sendSteps(n,up,dn){
  if(n===0)return;
  let c=n>0?up:dn;
  let cnt=Math.abs(n);
  (function step(i){
    if(i>=cnt)return update();
    fetch('/cmd/'+c).then(()=>step(i+1));
  })(0);
}

function recallMem(slot){
  fetch('/memory/recall?slot='+slot).then(r=>r.json()).then(d=>{
    if(d.ok){loadOptions();update();}
  }).catch(e=>console.error(e));
}

let deleteSlotPending=0;
function deleteMem(slot){
  deleteSlotPending=slot;
  document.getElementById('deleteSlotNum').textContent='#'+slot;
  document.getElementById('deleteModal').classList.add('open');
}
function closeDeleteModal(e){
  if(e&&e.target!==e.currentTarget)return;
  document.getElementById('deleteModal').classList.remove('open');
}
function confirmDelete(){
  fetch('/memory/set?slot='+deleteSlotPending+'&band=FM&freq=0&mode=FM')
    .then(r=>r.json()).then(d=>{if(d.ok)loadMem();});
  closeDeleteModal();
}

function quickAddMem(){
  fetch('/status').then(r=>r.json()).then(st=>{
    fetch('/memory/list').then(r=>r.json()).then(mem=>{
      let slot=1;
      for(let i=1;i<=200;i++){if(!mem.memories.find(m=>m.slot===i&&m.freq>0)){slot=i;break;}}
      let freqDisp=st.frequencyDisplay||'';
      let isFM=freqDisp.includes('MHz');
      let freqNum=parseFloat(freqDisp.replace(/[^0-9.]/g,''));
      let freqHz=isFM?Math.round(freqNum*1000000):Math.round(freqNum*1000);
      let mode=st.mode||'FM';
      let band=st.band||'FM';
      fetch('/memory/set?slot='+slot+'&band='+encodeURIComponent(band)+'&freq='+freqHz+'&mode='+encodeURIComponent(mode))
        .then(r=>r.json()).then(d=>{
          if(d.ok){loadMem();alert('Saved to slot '+slot);}
          else alert(d.error||'Error saving');
        });
    });
  }).catch(e=>alert('Error: '+e));
}

function showEditForm(slot){
  let m=memData.memories.find(x=>x.slot===slot)||{freq:0,band:'FM',mode:'FM',name:'',fav:false};
  let isFM=m.mode==='FM';
  let dispFreq=isFM?(m.freq/1000000).toFixed(2):(m.freq/1000).toString();
  let bandOpts=memData.bands.map(b=>'<option'+(m.band===b?' selected':'')+'>'+b+'</option>').join('');
  let el=document.getElementById('slot-'+slot);
  el.innerHTML=
    '<div class="slot-number">'+String(slot).padStart(2,'0')+'</div>'+
    '<div class="edit-form">'+
      '<input class="edit-input" type="text" id="freq-'+slot+'" value="'+dispFreq+'" placeholder="Freq">'+
      '<select class="edit-select" id="unit-'+slot+'">'+
        '<option'+(isFM?' selected':'')+'>MHz</option>'+
        '<option'+(isFM?'':' selected')+'>kHz</option>'+
      '</select>'+
      '<select class="edit-select" id="band-'+slot+'">'+bandOpts+'</select>'+
      '<input class="edit-input" type="text" id="name-'+slot+'" value="'+(m.name||'')+'" placeholder="Name" maxlength="11" style="width:80px">'+
      '<label style="display:flex;align-items:center;gap:4px;cursor:pointer">'+
        '<input type="checkbox" id="fav-'+slot+'"'+(m.fav?' checked':'')+'>'+'<span style="color:#fbbf24">★</span>'+
      '</label>'+
    '</div>'+
    '<div class="slot-actions">'+
      '<button class="btn btn-xs btn-primary" onclick="saveMem('+slot+')">Save</button>'+
      '<button class="btn btn-xs" onclick="loadMem()">X</button>'+
    '</div>';
}

function showAddForm(){
  let slot=1;
  for(let i=1;i<=200;i++){if(!memData.memories.find(m=>m.slot===i&&m.freq>0)){slot=i;break;}}
  let bandOpts=memData.bands.map(b=>'<option>'+b+'</option>').join('');
  let h='<div class="memory-slot" id="slot-new">'+
    '<div class="slot-number">'+String(slot).padStart(2,'0')+'</div>'+
    '<div class="edit-form">'+
      '<input class="edit-input" type="text" id="freq-new" placeholder="106.5">'+
      '<select class="edit-select" id="unit-new"><option>MHz</option><option>kHz</option></select>'+
      '<select class="edit-select" id="band-new">'+bandOpts+'</select>'+
      '<input class="edit-input" type="text" id="slot-num" value="'+slot+'" style="width:40px" placeholder="#">'+
    '</div>'+
    '<div class="slot-actions">'+
      '<button class="btn btn-xs btn-primary" onclick="saveNewMem()">Add</button>'+
      '<button class="btn btn-xs" onclick="loadMem()">X</button>'+
    '</div>'+
  '</div>';
  document.getElementById('memList').insertAdjacentHTML('beforeend',h);
}

function saveMem(slot){
  let freqStr=document.getElementById('freq-'+slot).value;
  let unit=document.getElementById('unit-'+slot).value;
  let band=document.getElementById('band-'+slot).value;
  let name=document.getElementById('name-'+slot).value;
  let fav=document.getElementById('fav-'+slot).checked;
  let mode=unit==='MHz'?'FM':'AM';
  let freq=parseFloat(freqStr);
  if(unit==='MHz')freq=Math.round(freq*1000000);else freq=Math.round(freq*1000);
  fetch('/memory/set?slot='+slot+'&band='+encodeURIComponent(band)+'&freq='+freq+'&mode='+encodeURIComponent(mode)+'&name='+encodeURIComponent(name)+'&fav='+(fav?'true':'false'))
    .then(r=>r.json()).then(d=>{if(d.ok)loadMem();else alert(d.error||'Error');});
}

function saveNewMem(){
  let slot=parseInt(document.getElementById('slot-num').value)||1;
  let freqStr=document.getElementById('freq-new').value;
  let unit=document.getElementById('unit-new').value;
  let band=document.getElementById('band-new').value;
  let mode=unit==='MHz'?'FM':'AM';
  let freq=parseFloat(freqStr);
  if(unit==='MHz')freq=Math.round(freq*1000000);else freq=Math.round(freq*1000);
  fetch('/memory/set?slot='+slot+'&band='+encodeURIComponent(band)+'&freq='+freq+'&mode='+encodeURIComponent(mode))
    .then(r=>r.json()).then(d=>{if(d.ok)loadMem();else alert(d.error||'Error');});
}

let memFilter='all';
function applyMemFilter(){
  memFilter=document.getElementById('memFilter').value;
  renderMemList();
}
function toggleFav(slot){
  let m=memData.memories.find(x=>x.slot===slot);
  if(!m)return;
  let newFav=!m.fav;
  fetch('/memory/set?slot='+slot+'&band='+encodeURIComponent(m.band)+'&freq='+m.freq+'&mode='+encodeURIComponent(m.mode)+'&name='+encodeURIComponent(m.name||'')+'&fav='+(newFav?'true':'false'))
    .then(r=>r.json()).then(d=>{if(d.ok)loadMem();});
}
function renderMemList(){
  let filled=memData.memories.filter(m=>m.freq>0);
  if(memFilter==='fav')filled=filled.filter(m=>m.fav);
  else if(memFilter!=='all')filled=filled.filter(m=>m.band===memFilter);
  filled.sort((a,b)=>(b.fav?1:0)-(a.fav?1:0)||(a.slot-b.slot));
  let h='';
  filled.forEach(m=>{
    let f=m.mode==='FM'?(m.freq/1000000).toFixed(2)+' MHz':(m.freq/1000)+' kHz';
    let nameDisp=m.name?'<div class="slot-name">'+m.name+'</div>':'';
    let favStar=m.fav?'<span style="color:#fbbf24;cursor:pointer" onclick="toggleFav('+m.slot+')">★</span>':'<span style="color:#475569;cursor:pointer" onclick="toggleFav('+m.slot+')">☆</span>';
    h+='<div class="memory-slot" id="slot-'+m.slot+'">';
    h+='<div class="slot-number">'+favStar+' '+String(m.slot).padStart(3,'0')+'</div>';
    h+='<div class="slot-info"><div class="slot-freq">'+f+'</div>'+nameDisp+'<div class="slot-meta">'+m.band+' '+m.mode+'</div></div>';
    h+='<div class="slot-actions">';
    h+='<button class="btn btn-xs btn-primary" title="Tune" onclick="recallMem('+m.slot+')"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg></button>';
    h+='<button class="btn btn-xs" title="Edit" onclick="showEditForm('+m.slot+')"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg></button>';
    h+='<button class="btn btn-xs btn-danger" title="Delete" onclick="deleteMem('+m.slot+')"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg></button>';
    h+='</div></div>';
  });
  if(filled.length===0)h='<div class="slot-empty">No matching memory slots</div>';
  document.getElementById('memList').innerHTML=h;
}
function loadMem(){
  fetch('/memory/list').then(r=>r.json()).then(d=>{
    memData=d;
    let filled=d.memories.filter(m=>m.freq>0);
    document.getElementById('memCount').textContent=filled.length;
    let filterEl=document.getElementById('memFilter');
    let curVal=filterEl.value;
    let bands=[...new Set(filled.map(m=>m.band))];
    filterEl.innerHTML='<option value="all">All Memories ('+filled.length+')</option>';
    filterEl.innerHTML+='<option value="fav">Favorites ('+filled.filter(m=>m.fav).length+')</option>';
    bands.forEach(b=>{
      let cnt=filled.filter(m=>m.band===b).length;
      filterEl.innerHTML+='<option value="'+b+'">'+b+' ('+cnt+')</option>';
    });
    filterEl.value=curVal||'all';
    renderMemList();
  });
}

function update(){
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('freq').textContent=d.frequencyDisplay.split(' ')[0];
    document.getElementById('unit').textContent=d.frequencyDisplay.split(' ')[1]||'MHz';
    radioVol=d.volume;radioBrt=d.brightness;
    document.getElementById('vol').textContent=d.volume;
    document.getElementById('vol-slider').value=d.volume;
    document.getElementById('brt').textContent=d.brightness;
    document.getElementById('brt-slider').value=d.brightness;
    document.getElementById('rssi').textContent=d.rssi+' dBuV';
    document.getElementById('snr').textContent=d.snr+' dB';
    document.getElementById('rssi-bar').style.width=Math.min(d.rssi,127)/127*100+'%';
    document.getElementById('snr-bar').style.width=Math.min(d.snr,50)/50*100+'%';
    let batPct=Math.max(0,Math.min(100,(d.voltage-3.3)/(4.2-3.3)*100));
    document.getElementById('bat-fill').setAttribute('width',Math.round(batPct/100*16));
    document.getElementById('voltage').textContent=d.voltage+'V';
    document.getElementById('sel-band').value=d.band;
    document.getElementById('sel-mode').value=d.mode;
    document.getElementById('sel-step').value=d.step;
    document.getElementById('sel-bw').value=d.bandwidth;
    document.getElementById('sel-agc').value=d.agc;
    let rdsEl=document.getElementById('rds-section');
    if(d.mode==='FM' && (d.stationName || d.radioText)){
      rdsEl.style.display='block';
      document.getElementById('rds-station').textContent=d.stationName||'';
      document.getElementById('rds-text').textContent=d.radioText||'';
      document.getElementById('rds-pty').textContent=d.programType||'';
      document.getElementById('rds-pty').style.display=d.programType?'inline':'none';
      document.getElementById('rds-pi').textContent=d.piCode?('PI:'+d.piCode.toString(16).toUpperCase().padStart(4,'0')):'';
      document.getElementById('rds-pi').style.display=d.piCode?'inline':'none';
    }else{
      rdsEl.style.display='none';
    }
  }).catch(e=>console.error(e));
}

document.addEventListener('DOMContentLoaded',function(){
  update();
  loadOptions();
  loadMem();
  setInterval(update,2000);
});

// Spectrum Modal functions
function openSpectrumModal(){
  document.getElementById('spectrumModal').classList.add('open');
  if(scanData&&scanData.data)drawSpectrum(false);
}
function closeSpectrumModal(e){
  if(e&&e.target!==e.currentTarget)return;
  document.getElementById('spectrumModal').classList.remove('open');
}

// Spectrum Analyzer - Full band progressive scanning
let scanData=null,fullScanData=[],bandInfo=null,scanning=false,nextFreq=0;
let scanStartTime=0,scanTimerInterval=null;

function formatTime(ms){
  let s=Math.floor(ms/1000);
  let m=Math.floor(s/60);s=s%60;
  return m+':'+(s<10?'0':'')+s;
}

function updateScanTimer(){
  if(!scanning||!bandInfo)return;
  let elapsed=Date.now()-scanStartTime;
  let progress=(nextFreq-bandInfo.minFreq)/(bandInfo.maxFreq-bandInfo.minFreq);
  if(progress>0){
    let totalEst=elapsed/progress;
    let remaining=totalEst-elapsed;
    document.getElementById('scanTimer').textContent=formatTime(elapsed)+' / ~'+formatTime(remaining)+' left';
  }else{
    document.getElementById('scanTimer').textContent=formatTime(elapsed);
  }
}

function runScan(){
  fetch('/scan/band').then(r=>r.json()).then(d=>{
    if(d.band==='ALL'){
      alert('Cannot scan ALL band - range too large. Please select a specific band.');
      return;
    }
    document.getElementById('scanBtn').style.display='none';
    document.getElementById('stopBtn').style.display='';
    document.getElementById('scanStatus').textContent='Scanning...';
    document.getElementById('scanTimer').textContent='';
    document.getElementById('miniScanBtn').style.display='none';
    document.getElementById('miniStopBtn').style.display='';
    document.getElementById('miniScanStatus').textContent='Scanning...';
    fullScanData=[];scanning=true;
    scanStartTime=Date.now();
    scanTimerInterval=setInterval(updateScanTimer,500);
    bandInfo=d;
    nextFreq=d.minFreq;
    startNextChunk();
  }).catch(e=>{
    console.error(e);
    document.getElementById('scanStatus').textContent='Error';
    document.getElementById('miniScanStatus').textContent='Error';
  });
}

function stopScan(){
  scanning=false;
  clearInterval(scanTimerInterval);
  document.getElementById('scanBtn').style.display='';
  document.getElementById('stopBtn').style.display='none';
  document.getElementById('scanStatus').textContent='Stopped';
  document.getElementById('miniScanBtn').style.display='';
  document.getElementById('miniStopBtn').style.display='none';
  document.getElementById('miniScanStatus').textContent='Stopped';
}

function startNextChunk(){
  if(!scanning||!bandInfo||nextFreq>bandInfo.maxFreq){
    scanning=false;
    clearInterval(scanTimerInterval);
    document.getElementById('scanBtn').style.display='';document.getElementById('stopBtn').style.display='none';
    document.getElementById('scanStatus').textContent='Complete';
    document.getElementById('miniScanBtn').style.display='';document.getElementById('miniStopBtn').style.display='none';
    document.getElementById('miniScanStatus').textContent='Complete';
    return;
  }
  let pct=Math.round((nextFreq-bandInfo.minFreq)/(bandInfo.maxFreq-bandInfo.minFreq)*100);
  document.getElementById('scanStatus').textContent='Scanning... '+pct+'%';
  document.getElementById('miniScanStatus').textContent=pct+'%';
  fetch('/scan/run?start='+nextFreq+'&step='+bandInfo.step+'&points=50')
    .then(r=>r.json()).then(d=>{
      if(d.ok)setTimeout(loadChunkData,200);
      else{scanning=false;document.getElementById('scanStatus').textContent='Error';document.getElementById('scanBtn').style.display='';document.getElementById('stopBtn').style.display='none';document.getElementById('miniScanBtn').style.display='';document.getElementById('miniStopBtn').style.display='none';}
    }).catch(e=>{console.error(e);scanning=false;document.getElementById('scanBtn').style.display='';document.getElementById('stopBtn').style.display='none';document.getElementById('miniScanBtn').style.display='';document.getElementById('miniStopBtn').style.display='none';});
}

function loadChunkData(){
  if(!scanning)return;
  fetch('/scan/data').then(r=>r.json()).then(d=>{
    if(!d.ready){setTimeout(loadChunkData,150);return;}
    for(let i=0;i<d.data.length;i++){
      fullScanData.push({freq:d.startFreq+d.step*i,rssi:d.data[i][0],snr:d.data[i][1]});}
    nextFreq=d.startFreq+d.step*d.count;
    updateFullSpectrum();
    if(scanning&&nextFreq<=bandInfo.maxFreq)startNextChunk();
    else{scanning=false;clearInterval(scanTimerInterval);document.getElementById('scanBtn').style.display='';document.getElementById('stopBtn').style.display='none';document.getElementById('scanStatus').textContent='Complete';document.getElementById('miniScanBtn').style.display='';document.getElementById('miniStopBtn').style.display='none';document.getElementById('miniScanStatus').textContent='Complete';}
  }).catch(e=>{console.error(e);scanning=false;clearInterval(scanTimerInterval);document.getElementById('scanBtn').style.display='';document.getElementById('stopBtn').style.display='none';document.getElementById('miniScanBtn').style.display='';document.getElementById('miniStopBtn').style.display='none';});
}

function updateFullSpectrum(){
  if(!fullScanData.length||!bandInfo)return;
  scanData={startFreq:bandInfo.minFreq,step:bandInfo.step,count:fullScanData.length,
    mode:bandInfo.mode,band:bandInfo.band,data:fullScanData.map(p=>[p.rssi,p.snr])};
  let isFM=bandInfo.mode==='FM';
  let startDisp=isFM?(bandInfo.minFreq/100).toFixed(1):bandInfo.minFreq;
  let endFreq=fullScanData[fullScanData.length-1].freq;
  let endDisp=isFM?(endFreq/100).toFixed(1):endFreq;
  let unit=isFM?' MHz':' kHz';
  document.getElementById('scanBand').textContent='Band: '+bandInfo.band+' ('+bandInfo.mode+')';
  document.getElementById('scanRange').textContent='Range: '+startDisp+' - '+endDisp+unit;
  document.getElementById('scanPoints').textContent='Points: '+fullScanData.length;
  drawSpectrum(true);
  drawMiniSpectrum();
}

// Fixed pixels per data point for scrollable spectrum
let pxPerPoint=6;
let spectrumPad={t:25,r:10,b:30,l:45};
let clickedIdx=-1;
let isDragging=false;

function drawSpectrum(autoScroll){
  if(!scanData||!scanData.data||!scanData.data.length)return;
  let canvas=document.getElementById('spectrumCanvas');
  let ctx=canvas.getContext('2d');
  let dpr=window.devicePixelRatio||1;
  let pw=scanData.data.length*pxPerPoint;
  let w=pw+spectrumPad.l+spectrumPad.r;
  let h=350;
  canvas.width=w*dpr;canvas.height=h*dpr;
  canvas.style.width=w+'px';canvas.style.height=h+'px';
  ctx.scale(dpr,dpr);
  let ph=h-spectrumPad.t-spectrumPad.b;
  ctx.fillStyle='#0f0f1a';
  ctx.fillRect(0,0,w,h);
  let rssiArr=scanData.data.map(p=>p[0]);
  let minR=Math.min(...rssiArr),maxR=Math.max(...rssiArr);
  if(maxR-minR<10){minR-=5;maxR+=5;}
  let peaks=[];
  let threshold=minR+(maxR-minR)*0.3;
  let minDist=Math.max(3,Math.floor(scanData.data.length/30));
  for(let i=2;i<rssiArr.length-2;i++){
    if(rssiArr[i]>rssiArr[i-1]&&rssiArr[i]>rssiArr[i+1]&&
       rssiArr[i]>rssiArr[i-2]&&rssiArr[i]>rssiArr[i+2]&&
       rssiArr[i]>=threshold){
      let tooClose=false;
      for(let p of peaks){if(Math.abs(p.idx-i)<minDist){tooClose=true;break;}}
      if(!tooClose)peaks.push({idx:i,rssi:rssiArr[i]});
    }
  }
  peaks.sort((a,b)=>b.rssi-a.rssi);
  if(peaks.length>8)peaks=peaks.slice(0,8);
  ctx.strokeStyle='#334155';ctx.lineWidth=0.5;
  for(let i=0;i<=5;i++){
    let y=spectrumPad.t+ph*(1-i/5);
    ctx.beginPath();ctx.moveTo(spectrumPad.l,y);ctx.lineTo(w-spectrumPad.r,y);ctx.stroke();
    let val=minR+(maxR-minR)*i/5;
    ctx.fillStyle='#64748b';ctx.font='10px sans-serif';ctx.textAlign='right';
    ctx.fillText(val.toFixed(0),spectrumPad.l-5,y+3);
  }
  ctx.textAlign='center';ctx.fillStyle='#64748b';ctx.font='10px sans-serif';
  let isFM=scanData.mode==='FM';
  let labelEvery=Math.max(1,Math.floor(300/(pxPerPoint*10)));
  for(let i=0;i<scanData.data.length;i+=labelEvery){
    let x=spectrumPad.l+i*pxPerPoint;
    let freq=scanData.startFreq+scanData.step*i;
    let lbl=isFM?(freq/100).toFixed(1):freq.toFixed(0);
    ctx.fillText(lbl,x,h-spectrumPad.b+15);
    ctx.beginPath();ctx.moveTo(x,spectrumPad.t);ctx.lineTo(x,h-spectrumPad.b);ctx.stroke();
  }
  ctx.fillStyle='#94a3b8';ctx.font='11px sans-serif';
  ctx.save();ctx.translate(12,h/2);ctx.rotate(-Math.PI/2);
  ctx.fillText('RSSI/SNR',0,0);ctx.restore();
  let snrArr=scanData.data.map(p=>p[1]);
  let minS=Math.min(...snrArr),maxS=Math.max(...snrArr);
  if(maxS-minS<10){minS-=5;maxS+=5;}
  let minVal=Math.min(minR,minS),maxVal=Math.max(maxR,maxS);
  ctx.beginPath();
  ctx.moveTo(spectrumPad.l,spectrumPad.t+ph);
  for(let i=0;i<scanData.data.length;i++){
    let x=spectrumPad.l+i*pxPerPoint;
    let y=spectrumPad.t+ph*(1-(scanData.data[i][1]-minVal)/(maxVal-minVal));
    ctx.lineTo(x,y);
  }
  ctx.lineTo(spectrumPad.l+(scanData.data.length-1)*pxPerPoint,spectrumPad.t+ph);
  ctx.closePath();
  ctx.fillStyle='rgba(34,197,94,0.15)';ctx.fill();
  ctx.beginPath();
  for(let i=0;i<scanData.data.length;i++){
    let x=spectrumPad.l+i*pxPerPoint;
    let y=spectrumPad.t+ph*(1-(scanData.data[i][1]-minVal)/(maxVal-minVal));
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.strokeStyle='#22c55e';ctx.lineWidth=1.5;ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(spectrumPad.l,spectrumPad.t+ph);
  for(let i=0;i<scanData.data.length;i++){
    let x=spectrumPad.l+i*pxPerPoint;
    let y=spectrumPad.t+ph*(1-(scanData.data[i][0]-minVal)/(maxVal-minVal));
    ctx.lineTo(x,y);
  }
  ctx.lineTo(spectrumPad.l+(scanData.data.length-1)*pxPerPoint,spectrumPad.t+ph);
  ctx.closePath();
  ctx.fillStyle='rgba(6,182,212,0.15)';ctx.fill();
  ctx.beginPath();
  for(let i=0;i<scanData.data.length;i++){
    let x=spectrumPad.l+i*pxPerPoint;
    let y=spectrumPad.t+ph*(1-(scanData.data[i][0]-minVal)/(maxVal-minVal));
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.strokeStyle='#06b6d4';ctx.lineWidth=1.5;ctx.stroke();
  ctx.font='9px monospace';ctx.textAlign='center';
  for(let p of peaks){
    let px=spectrumPad.l+p.idx*pxPerPoint;
    let py=spectrumPad.t+ph*(1-(p.rssi-minVal)/(maxVal-minVal));
    let pFreq=scanData.startFreq+scanData.step*p.idx;
    let pLbl=isFM?(pFreq/100).toFixed(2):pFreq.toFixed(0);
    ctx.beginPath();ctx.arc(px,py,3,0,Math.PI*2);
    ctx.fillStyle='#fbbf24';ctx.fill();
    ctx.strokeStyle='#fbbf24';ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(px,py-4);ctx.lineTo(px,spectrumPad.t+8);ctx.stroke();
    ctx.fillStyle='#fbbf24';
    ctx.fillText(pLbl,px,spectrumPad.t+6);
  }
  if(clickedIdx>=0&&clickedIdx<scanData.data.length){
    let clickX=spectrumPad.l+clickedIdx*pxPerPoint;
    let clickFreq=scanData.startFreq+scanData.step*clickedIdx;
    let clickLbl=isFM?(clickFreq/100).toFixed(2)+' MHz':clickFreq.toFixed(0)+' kHz';
    ctx.strokeStyle='#f97316';ctx.lineWidth=2;
    ctx.beginPath();ctx.moveTo(clickX,spectrumPad.t);ctx.lineTo(clickX,h-spectrumPad.b);ctx.stroke();
    ctx.fillStyle='#f97316';ctx.font='bold 12px monospace';ctx.textAlign='center';
    ctx.fillText(clickLbl,clickX,spectrumPad.t-5);
    ctx.beginPath();ctx.moveTo(clickX-5,h-spectrumPad.b);ctx.lineTo(clickX+5,h-spectrumPad.b);ctx.lineTo(clickX,h-spectrumPad.b-8);ctx.closePath();
    ctx.fillStyle='#f97316';ctx.fill();
  }
  if(autoScroll){
    let wrap=canvas.parentElement;
    wrap.scrollLeft=wrap.scrollWidth;
  }
}

function getIdxFromEvent(e){
  if(!scanData||!scanData.data||!scanData.data.length)return -1;
  let canvas=document.getElementById('spectrumCanvas');
  let rect=canvas.getBoundingClientRect();
  let scaleX=canvas.width/rect.width;
  let dpr=window.devicePixelRatio||1;
  let x=(e.clientX-rect.left)*scaleX/dpr;
  let idx=Math.round((x-spectrumPad.l)/pxPerPoint);
  if(idx<0)idx=0;if(idx>=scanData.data.length)idx=scanData.data.length-1;
  return idx;
}

function onMouseDown(e){
  let idx=getIdxFromEvent(e);
  if(idx<0)return;
  isDragging=true;
  clickedIdx=idx;
  drawSpectrum(false);
}

function onMouseMove(e){
  if(!isDragging)return;
  let idx=getIdxFromEvent(e);
  if(idx<0)return;
  clickedIdx=idx;
  drawSpectrum(false);
}

function onMouseUp(e){
  if(!isDragging)return;
  isDragging=false;
  let idx=getIdxFromEvent(e);
  if(idx<0)return;
  clickedIdx=idx;
  let freq=scanData.startFreq+scanData.step*idx;
  fetch('/tune?freq='+freq).then(r=>r.json()).then(d=>{
    if(d.ok){loadOptions();update();}
  });
  drawSpectrum(false);
}

function onMouseLeave(e){
  if(isDragging){
    isDragging=false;
    drawSpectrum(false);
  }
}

function zoomIn(){
  pxPerPoint=Math.min(20,pxPerPoint+2);
  if(scanData&&scanData.data)drawSpectrum(false);
}
function zoomOut(){
  pxPerPoint=Math.max(2,pxPerPoint-2);
  if(scanData&&scanData.data)drawSpectrum(false);
}

function drawMiniSpectrum(){
  if(!scanData||!scanData.data||!scanData.data.length)return;
  let canvas=document.getElementById('miniSpectrumCanvas');
  if(!canvas)return;
  let ctx=canvas.getContext('2d');
  let w=canvas.offsetWidth*2;let h=canvas.offsetHeight*2;
  canvas.width=w;canvas.height=h;
  ctx.fillStyle='#0a0a14';ctx.fillRect(0,0,w,h);
  let rssiArr=scanData.data.map(p=>p[0]);
  let minR=Math.min(...rssiArr),maxR=Math.max(...rssiArr);
  if(maxR-minR<10){minR-=5;maxR+=5;}
  let pad=4;let pw=w-pad*2;let ph=h-pad*2;
  let pxPer=pw/scanData.data.length;
  ctx.beginPath();
  for(let i=0;i<scanData.data.length;i++){
    let x=pad+i*pxPer;
    let y=pad+ph*(1-(scanData.data[i][0]-minR)/(maxR-minR));
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.strokeStyle='#06b6d4';ctx.lineWidth=2;ctx.stroke();
  ctx.beginPath();ctx.setLineDash([4,2]);
  for(let i=0;i<scanData.data.length;i++){
    let x=pad+i*pxPer;
    let y=pad+ph*(1-(scanData.data[i][1]-minR)/(maxR-minR));
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.strokeStyle='#22c55e';ctx.lineWidth=1.5;ctx.stroke();
  ctx.setLineDash([]);
}

document.addEventListener('DOMContentLoaded',function(){
  let miniCanvas=document.getElementById('miniSpectrumCanvas');
  if(miniCanvas)miniCanvas.addEventListener('click',openSpectrumModal);

  let specCanvas=document.getElementById('spectrumCanvas');
  if(specCanvas){
    specCanvas.addEventListener('mousedown',onMouseDown);
    specCanvas.addEventListener('mousemove',onMouseMove);
    specCanvas.addEventListener('mouseup',onMouseUp);
    specCanvas.addEventListener('mouseleave',onMouseLeave);
    specCanvas.style.userSelect='none';
  }
});

function saveSpectrumCSV(){
  if(!scanData||!scanData.data||!scanData.data.length){alert('No spectrum data to save');return;}
  let isFM=scanData.mode==='FM';
  let csv='Frequency,RSSI,SNR\n';
  for(let i=0;i<scanData.data.length;i++){
    let freq=scanData.startFreq+scanData.step*i;
    let dispFreq=isFM?(freq/100).toFixed(2):freq;
    csv+=dispFreq+','+scanData.data[i][0]+','+scanData.data[i][1]+'\n';
  }
  let blob=new Blob([csv],{type:'text/csv'});
  let a=document.createElement('a');
  a.href=URL.createObjectURL(blob);
  a.download='spectrum_'+scanData.band+'_'+new Date().toISOString().slice(0,10)+'.csv';
  a.click();
}

function saveMemCSV(){
  fetch('/memory/list').then(r=>r.json()).then(d=>{
    if(!d.memories||!d.memories.length){alert('No memories to export');return;}
    let csv='Slot,Band,Frequency,Mode,Name\n';
    for(let m of d.memories){
      if(m.freq>0){
        let name=m.name||'';
        name=name.replace(/"/g,'""');
        if(name.includes(','))name='"'+name+'"';
        csv+=m.slot+','+m.band+','+m.freq+','+m.mode+','+name+'\n';
      }
    }
    let blob=new Blob([csv],{type:'text/csv'});
    let a=document.createElement('a');
    a.href=URL.createObjectURL(blob);
    a.download='memories_'+new Date().toISOString().slice(0,10)+'.csv';
    a.click();
  });
}

function loadMemCSV(input){
  if(!input.files||!input.files[0])return;
  let reader=new FileReader();
  reader.onload=function(e){
    let lines=e.target.result.split('\n');
    let imported=0,errors=0;
    for(let i=1;i<lines.length;i++){
      let line=lines[i].trim();
      if(!line)continue;
      let parts=[];
      let inQuote=false,field='';
      for(let c of line){
        if(c==='"'){inQuote=!inQuote;}
        else if(c===','&&!inQuote){parts.push(field);field='';}
        else field+=c;
      }
      parts.push(field);
      if(parts.length>=4){
        let slot=parseInt(parts[0]);
        let band=parts[1];
        let freq=parseInt(parts[2]);
        let mode=parts[3];
        if(slot>0&&slot<=99&&freq>0){
          fetch('/memory/set?slot='+slot+'&band='+encodeURIComponent(band)+'&freq='+freq+'&mode='+encodeURIComponent(mode))
            .then(r=>r.json()).then(d=>{if(d.ok)imported++;else errors++;loadMem();});
        }else errors++;
      }
    }
    setTimeout(()=>{alert('Import complete: '+imported+' slots');loadMem();},500);
  };
  reader.readAsText(input.files[0]);
  input.value='';
}
)rawliteral";

#endif // WEB_SCRIPT_H
