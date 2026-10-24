// A time-based, bounded view of actual host events. No synthesized packets.
class ActivityTimeline {
  constructor(root) {
    this.root=root;this.canvas=root.querySelector('canvas');this.ctx=this.canvas.getContext('2d');
    this.status=root.querySelector('.activity-status');this.tooltip=root.querySelector('.activity-tooltip');
    this.description=root.querySelector('.activity-description');this.events=[];this.hits=[];
    this.serverNow=Date.now()/1000;this.anchor=performance.now();this.connected=false;this.stream=null;
    this.canvas.addEventListener('pointermove',event=>this.inspect(event));
    this.canvas.addEventListener('click',event=>this.inspect(event));
    this.canvas.addEventListener('pointerleave',()=>{this.tooltip.hidden=true;});
    new ResizeObserver(()=>this.resize()).observe(this.canvas);
    const interval=matchMedia('(prefers-reduced-motion: reduce)').matches?250:1000/30;
    let last=0;
    const frame=time=>{if(time-last>=interval){this.draw();last=time;}requestAnimationFrame(frame);};
    requestAnimationFrame(frame);
  }
  accept(data) {
    this.root.hidden=data.source!=='ground';this.connected=Boolean(data.connected);this.radio=data.radio;
    const log=data.activity;if(!log || !Number.isFinite(log.server_now)) return;
    this.serverNow=log.server_now;this.anchor=performance.now();
    this.events=log.events??[];this.window=log.window;this.dropped=log.dropped_events??0;
    this.stream=log.generation;this.root.querySelector('.activity-gap').hidden=!this.dropped;
    this.root.querySelector('.activity-gap').title=`${this.dropped} activity events missed; gaps are not filled in.`;
    const tail=this.events.filter(e=>e.kind==='packet').slice(-5).map(e=>this.describe(e));
    this.description.textContent=tail.length?tail.join('. '):'No packet activity observed yet. The timeline continues during silence.';
  }
  disconnect() { this.connected=false; }
  now() { return this.serverNow+(performance.now()-this.anchor)/1000; }
  resize() {
    const box=this.canvas.getBoundingClientRect(),dpr=devicePixelRatio||1;
    this.width=box.width;this.height=box.height;
    this.canvas.width=Math.round(box.width*dpr);this.canvas.height=Math.round(box.height*dpr);
    this.ctx.setTransform(dpr,0,0,dpr,0,0);
  }
  describe(e) {
    const actor=e.direction==='tx'?'Ground → balloon':'Balloon → ground';
    const kind=e.type==='control'?({1:'SYN',2:'ACK',3:'SYN-ACK'}[e.flags]??'Control'):e.type==='beacon'?'Beacon':'Data';
    const duration=e.duration<1?`${Math.round(e.duration*1000)} ms`:`${e.duration.toFixed(2)} s`;
    return `${actor} · ${kind} · ${e.profile} · ${duration}${e.restarted?' · Sender restarted (RP2040 or radio)':''}${e.direction==='tx'?' · sent locally; delivery unconfirmed':''}`;
  }
  inspect(event) {
    const box=this.canvas.getBoundingClientRect(),x=event.clientX-box.left,y=event.clientY-box.top;
    const hit=[...this.hits].reverse().find(h=>x>=h.x-3 && x<=h.x+h.width+3 && y>=h.y-4 && y<=h.y+h.height+4);
    if(!hit){this.tooltip.hidden=true;return;}
    this.tooltip.textContent=hit.text;this.tooltip.hidden=false;
    this.tooltip.style.left=`${Math.min(Math.max(12,event.clientX-this.root.getBoundingClientRect().left-110),Math.max(12,this.root.clientWidth-270))}px`;
  }
  draw() {
    if(this.root.hidden || !this.width) return;
    const c=this.ctx,w=this.width,h=this.height,now=this.now(),span=60;
    const x=t=>w-7-(now-t)*(w-14)/span;
    c.clearRect(0,0,w,h);this.hits=[];
    // Keep bounded client history even if the API stops responding.
    this.events=this.events.filter(e=>e.start+e.duration>=now-span);
    const bands={data:'rgba(217,123,40,.045)',control:'rgba(53,128,185,.08)',beacon:'rgba(218,173,28,.08)',acquiring:'rgba(110,130,140,.04)'};
    for(const e of this.events.filter(e=>e.kind==='window')) {
      const left=Math.max(0,x(e.start)),right=Math.min(w,x(Math.min(now,e.start+e.duration)));
      if(right>left){c.fillStyle=bands[e.phase]??'rgba(110,130,140,.025)';c.fillRect(left,0,right-left,41);}
      const line=x(e.start);
      if(line>=0 && line<=w){
        c.strokeStyle='rgba(104,119,125,.48)';c.lineWidth=1;c.beginPath();c.moveTo(line,1);c.lineTo(line,40);c.stroke();
        this.hits.push({x:line-1,y:0,width:2,height:40,text:`${e.phase[0].toUpperCase()+e.phase.slice(1)} window begins${e.phase==='control'?' · P0':` · ${e.profile}`}`});
        const label=e.phase==='control'?'CONTROL':e.phase==='data'?`DATA ${e.profile}`:e.phase.toUpperCase();
        if(line<w-80){c.fillStyle='#667d87';c.font='8px system-ui';c.fillText(label,line+5,9);}
      }
    }
    c.strokeStyle='rgba(108,132,140,.16)';c.lineWidth=1;
    for(const y of [27]){c.beginPath();c.moveTo(0,y);c.lineTo(w,y);c.stroke();}
    const colors={data:'#d98232',control:'#3687c2',beacon:'#d8ac24'};
    for(const e of this.events.filter(e=>e.kind==='packet')) {
      if(e.start>now) continue;
      const end=Math.min(now,e.start+e.duration),left=Math.max(0,x(e.start));
      const right=Math.min(w,x(end));if(right<0 || left>w) continue;
      const width=Math.max(3,right-left),y=22;
      c.globalAlpha=Math.max(0,Math.min(1,(e.start+e.duration-(now-span))/6))*Math.min(1,Math.max(.15,(now-e.received_at)/.2));
      c.fillStyle=e.restarted?'#8b1e24':colors[e.type]??'#8b989d';c.beginPath();c.roundRect(left,y,width,10,5);c.fill();
      if(e.type==='control' && width>=15){
        c.font='bold 7px system-ui';c.textAlign='center';c.fillStyle='#fff';
        c.fillText(({1:'S',2:'A',3:'SA'}[e.flags]??'C'),left+width/2,y+7.5);c.textAlign='left';
      }
      c.globalAlpha=1;
      this.hits.push({x:left,y,width,height:10,text:this.describe(e)});
    }
    // Absolute-time ruler ticks keep moving even when no packets arrive.
    c.strokeStyle='rgba(108,132,140,.28)';c.lineWidth=1;
    for(let tick=Math.ceil((now-span)/5)*5;tick<=now;tick+=5){const at=x(tick);c.beginPath();c.moveTo(at,41);c.lineTo(at,45);c.stroke();}
    c.fillStyle='#49616b';c.font='9px system-ui';
    for(const seconds of [60,45,30,15,0]){
      const at=x(now-seconds);c.textAlign=seconds===60?'left':seconds===0?'right':'center';
      c.fillText(seconds===0?'NOW':`−${seconds}s`,at,h-2);
    }
    c.textAlign='left';c.strokeStyle='#879ba3';c.beginPath();c.moveTo(w-7,13);c.lineTo(w-7,38);c.stroke();
    if(!this.connected){this.status.textContent='Ground disconnected';return;}
    const current=this.window;
    if(!current){this.status.textContent=({'1':'Listening for beacons','2':'Receiving data','4':'Control window'}[this.radio?.phase]??'Waiting for event feed');return;}
    const labels={data:`Data · ${current.profile}`,control:'Control · P0',beacon:'Beacons · P0',acquiring:'Acquiring timing',searching:'Listening for beacons',stopped:'Link stopped',fault:'Radio fault'};
    let label=labels[current.phase]??'Listening';
    if(current.duration>0){
      const remaining=current.start+current.duration-now;
      label+=remaining>0?` · ${Math.ceil(remaining)}s left`:' · awaiting window update';
    }
    this.status.textContent=label;
  }
}
