/* smoke-test rendu: stub DOM minimal + exercice de render() ouvert/fermé */
const noop=()=>{};
const ctx2d=new Proxy({}, { get:(t,k)=> k==='canvas'?{}:noop, set:()=>true });
const mkEl=()=>({ getContext:()=>ctx2d, style:{}, classList:{add:noop,remove:noop,toggle:noop},
  appendChild:noop, addEventListener:noop, onchange:null, onclick:null, onwheel:null,
  onmousemove:null, onmouseup:null, onmousedown:null, checked:true, value:'10', textContent:'' });
const shared=mkEl(); shared.prepend=noop; shared.lastChild=null; shared.childElementCount=0;
global.document={ getElementById:()=>shared, createElement:mkEl, addEventListener:noop,
  querySelectorAll:()=>[] };
global.window={ addEventListener:noop };
global.localStorage={ getItem:()=>null, setItem:noop, removeItem:noop };
global.requestAnimationFrame=noop;
require('/tmp/opencode/sim.js');
const H=globalThis.__SIM_HOOK__;
if(!H) { console.error("hook absent"); process.exit(1); }
let n=0; const A=(c,m)=>{ if(!c){ console.error("FAIL:",m); process.exit(1);} n++; };
H.render(); A(true,"render fermé");
H.open(); A(H.state().open===true,"éditeur ouvert");
H.render(); n++;                                            /* rendu ouvert: 4 lignes + surbrillance */
for(let k=0;k<4;k++){ H.turn(k,1024,false); H.render(); }   /* chaque potard +1 cran, rendu */
for(let k=0;k<4;k++){ H.turn(k,-1024,false); H.render(); }
H.setShift(1); H.turn(0,1024,1); H.render();                /* canal (Shift+bleu) */
H.press(59,1,1); H.render();                                /* learn armé -> LRN */
H.press(62,1,1); H.render();                                /* suppression ligne */
H.dispatch("CC",80,1,127); H.render();                      /* dispatch + rendu */
H.setShift(0); H.close(); H.render();
console.log("render smoke-test: "+n+" rendus OK (aucune exception)");
