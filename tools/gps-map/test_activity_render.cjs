const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {test} = require('node:test');

const sandbox = vm.createContext({});
vm.runInContext(fs.readFileSync(`${__dirname}/static/activity.js`, 'utf8'), sandbox);
const Timeline = vm.runInContext('ActivityTimeline', sandbox);

function render(type, restarted, direction='rx', flags=restarted?128:0) {
  const fills=[];
  const ctx={};
  for(const name of ['clearRect','beginPath','moveTo','lineTo','stroke','roundRect','fillText']) ctx[name]=()=>{};
  ctx.fill=()=>fills.push(ctx.fillStyle);
  const timeline=Object.create(Timeline.prototype);
  Object.assign(timeline,{root:{hidden:false},ctx,width:600,height:57,status:{},
    connected:true,now:()=>100,events:[{kind:'packet',type,restarted,direction,
      flags,profile:'P0',start:98,duration:1,received_at:99}]});
  timeline.draw();
  return {color:fills[0],tooltip:timeline.hits[0].text};
}

test('restart data/empty-control packets are dark red; beacons and handshakes keep their colors',()=>{
  for(const type of ['data','control']) {
    const marked=render(type,true);
    assert.equal(marked.color,'#8b1e24');
    assert.match(marked.tooltip,/Sender restarted \(RP2040 or radio\)/);
    assert.notEqual(render(type,false).color,marked.color);
    assert.doesNotMatch(render(type,false).tooltip,/restarted/);
  }
  assert.equal(render('beacon',false).color,'#d8ac24');
  for(const [flags,label] of [[1,'SYN'],[3,'SYN-ACK'],[2,'ACK']]) {
    const packet=render('control',false,'rx',flags);
    assert.equal(packet.color,'#3687c2');assert.ok(packet.tooltip.includes(label));
    assert.doesNotMatch(packet.tooltip,/restarted/);
  }
  assert.match(render('control',true,'tx').tooltip,/sent locally; delivery unconfirmed/);
});
