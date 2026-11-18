const red=[255,0,0], white=[255,255,255];
const fixtures=[
 {name:'set replaces an existing transform',script:'c.translate(60,0);c.setTransform(2,0,0,1,10,0);c.fillRect(0,0,10,20);',pixels:[[15,10,red],[75,10,white],[35,10,white]]},
 {name:'transform composes in canvas matrix order',script:'c.translate(10,0);c.transform(2,0,0,1,5,0);c.fillRect(0,0,10,20);',pixels:[[20,10,red],[5,10,white],[40,10,white]]},
 {name:'off-diagonal coefficients keep their correct axes',script:'c.setTransform(0,1,-1,0,40,0);c.fillRect(0,0,10,20);',pixels:[[25,5,red],[45,5,white],[25,15,white]]},
 {name:'reset preserves style and clipping',script:'c.beginPath();c.rect(0,0,5,20);c.clip();c.translate(60,0);c.resetTransform();c.fillRect(0,0,10,20);',pixels:[[2,5,red],[7,5,white],[65,5,white]]},
 {name:'dictionary aliases and defaults',script:'c.setTransform({a:2,m11:2,m41:10,m42:5});c.fillRect(0,0,10,20);',pixels:[[15,10,red],[5,10,white],[35,10,white]]},
 {name:'nonfinite operations leave the existing matrix intact',script:'c.translate(10,0);c.setTransform(NaN,0,0,1,40,0);c.transform(Infinity,0,0,1,40,0);c.setTransform({a:NaN,m11:NaN});c.fillRect(0,0,10,20);',pixels:[[15,10,red],[5,10,white],[45,10,white]]},
 {name:'conversion errors and inconsistent aliases preserve state',script:`c.translate(10,0);let alias=false,conversion=false,arity=false,receiver=false;try{c.setTransform({a:1,m11:2})}catch(e){alias=e instanceof TypeError}const sentinel=new Error('conversion');try{c.setTransform({a:{valueOf(){throw sentinel}}})}catch(e){conversion=e===sentinel}try{c.transform(1,0)}catch(e){arity=e instanceof TypeError}try{c.setTransform.call({},NaN,0,0,1,0,0)}catch(e){receiver=e instanceof TypeError}console.log('CANVAS_ERRORS',alias,conversion,arity,receiver);if(!alias||!conversion||!arity||!receiver)throw new Error('matrix error contract');c.fillRect(0,0,10,20);`,pixels:[[15,10,red],[5,10,white]],log:'CANVAS_ERRORS true true true true'},
 {name:'save restore and resize retain their matrix contracts',script:'c.translate(10,0);c.save();c.setTransform(2,0,0,2,50,0);c.restore();c.fillRect(0,0,10,20);c.save();c.setTransform(2,0,0,2,50,0);canvas.width=canvas.width;c.fillStyle="red";c.fillRect(0,0,10,20);c.restore();c.fillRect(0,30,10,10);',pixels:[[5,10,red],[15,10,white],[5,35,red],[55,10,white]]},
 {name:'load callback can initialize a resized canvas',callback:true,script:'canvas.width=120;canvas.height=80;c.setTransform(window.devicePixelRatio||1,0,0,window.devicePixelRatio||1,0,0);c.fillStyle="red";c.beginPath();c.arc(15,15,5,0,Math.PI*2);c.fill();console.log("CANVAS_CALLBACK_READY");',pixels:[[15,15,red],[30,15,white]],log:'CANVAS_CALLBACK_READY'},
 {name:'no-argument setTransform resets to identity',script:'c.translate(50,0);c.setTransform();c.fillRect(0,0,10,20);',pixels:[[5,10,red],[55,10,white]]},
 {
  name: 'getTransform copies the current matrix through save, restore and resize',
  script: `
    const check = (condition, label) => { if (!condition) throw new Error(label); };
    const initial = c.getTransform();
    check(initial instanceof DOMMatrix && initial.is2D && initial.isIdentity, 'identity brand');
    c.setTransform(2, 3, 4, 5, 6, 7);
    c.save();
    const saved = c.getTransform();
    check([saved.m11, saved.m12, saved.m21, saved.m22, saved.m41, saved.m42].join() === '2,3,4,5,6,7', 'coefficients');
    saved.m41 = 40;
    check(saved.e === 40 && c.getTransform().e === 6 && initial.isIdentity, 'independent copy');
    c.resetTransform();
    check(c.getTransform().isIdentity && saved.m41 === 40, 'reset leaves the saved matrix alone');
    c.restore();
    check(c.getTransform().m41 === 6, 'restore');
    canvas.width = canvas.width;
    check(c.getTransform().isIdentity, 'resize');
    let rejected = false;
    try { c.getTransform.call({}); } catch (error) { rejected = error instanceof TypeError; }
    check(rejected, 'receiver brand');
    c.translate(40, 0);
    const translated = c.getTransform();
    translated.a = 2;
    check(translated.m11 === 2, 'long alias reads short name');
    c.setTransform(translated);
    c.fillStyle = 'red';
    c.fillRect(0, 0, 10, 10);
    console.log('CANVAS_TRANSFORM_SNAPSHOT_READY');`,
  pixels: [[55, 5, red], [35, 5, white], [65, 5, white]],
  log: 'CANVAS_TRANSFORM_SNAPSHOT_READY'
 },
 {
  name: 'ellipse paints rotated and counterclockwise arcs',
  script: `
    c.beginPath();
    c.ellipse(30, 30, 19, 6, Math.PI / 2, 0, 2 * Math.PI);
    c.fill();
    c.beginPath();
    c.ellipse(85, 30, 20, 12, 0, 0, Math.PI, true);
    c.fill();
    c.beginPath();
    c.ellipse(130, 30, 15, 10, 0, -1e300, 1e300);
    c.fill();`,
  pixels: [[30, 45, red], [40, 30, white], [85, 24, red], [85, 36, white], [130, 30, red]]
 },
 {
  name: 'arc and ellipse validate radii and normalize large reverse sweeps',
  script: `
    function throwsNamed(call, name) {
      try { call(); } catch (error) { return error.name === name; }
      return false;
    }
    const ellipse = throwsNamed(() => c.ellipse(5, 5, 3, -1, 0, 0, 1), 'IndexSizeError');
    const arc = throwsNamed(() => c.arc(5, 5, -1, 0, 1), 'IndexSizeError');
    const arity = throwsNamed(() => c.ellipse(0, 0, 1, 1, 0, 0), 'TypeError');
    c.beginPath();
    c.ellipse(5, 5, 3, -1, NaN, 0, 1);
    c.arc(70, 30, 1, 1e300, -1e300);
    c.fill();
    if (ellipse && arc && arity) c.fillRect(15, 15, 10, 10);
    console.log('CANVAS_ELLIPSE_VALIDATION', ellipse, arc, arity);`,
  pixels: [[20, 20, red], [5, 5, white]],
  log: 'CANVAS_ELLIPSE_VALIDATION true true true'
 },
 {
  name: 'arcTo joins tangent segments and handles empty paths and zero radius',
  script: `
    c.beginPath();
    c.moveTo(10, 10);
    c.arcTo(50, 10, 50, 50, 20);
    c.lineTo(50, 50);
    c.lineTo(10, 50);
    c.closePath();
    c.fill();
    c.beginPath();
    c.arcTo(70, 10, 100, 10, 12);
    c.arcTo(100, 10, 100, 50, 0);
    c.lineTo(100, 50);
    c.lineTo(70, 50);
    c.closePath();
    c.fill();
    let rejected = false;
    try { c.arcTo(120, 10, 120, 50, -1); } catch (error) {
      rejected = error.name === 'IndexSizeError';
    }
    if (rejected) c.fillRect(120, 10, 10, 10);
    console.log('CANVAS_ARCTO_READY', rejected);`,
  pixels: [[45, 22, red], [48, 12, white], [98, 12, red], [125, 15, red]],
  log: 'CANVAS_ARCTO_READY true'
 }
];
function fixtureHtml(f){const script='var canvas=document.getElementById("canvas"),c=canvas.getContext("2d");c.fillStyle="red";'+f.script;return '<!doctype html><style>body{margin:0}canvas{display:block}</style><canvas id="canvas" width="160" height="80"></canvas><script>'+(f.callback?'document.addEventListener("DOMContentLoaded",function(){'+script+'});':script)+'</script>';}
module.exports={fixtures,fixtureHtml};
