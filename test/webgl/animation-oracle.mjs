// Regenerate with node test/webgl/animation-oracle.mjs; the pinned upstream engine is the oracle.
import * as THREE from '../demo/scene3d/vendor/three/three.core.js';
import { writeFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const tracks = [];
const samples = [-0.2, 0, 0.125, 0.5, 0.75, 1, 1.25, 1.75, 2, 3];
for (const interpolation of [THREE.InterpolateDiscrete, THREE.InterpolateLinear, THREE.InterpolateSmooth, THREE.InterpolateBezier]) {
  for (const [kind, Class, components, values] of [
    [0, THREE.NumberKeyframeTrack, 1, [2, 9, -3, 4]],
    [1, THREE.VectorKeyframeTrack, 3, [2, 3, 4, 9, -1, 0, -3, 4, 8, 4, 0, 2]],
    [2, THREE.ColorKeyframeTrack, 3, [0.2, 0.3, 0.4, 0.9, 0.1, 0, 0.3, 0.4, 0.8, 0.4, 0, 0.2]],
  ]) {
    const times = [0, 0.5, 1.25, 2];
    const track = new Class('.value', times, values, interpolation);
    const incoming = [], outgoing = [];
    for (let key = 0; key < times.length; ++key) for (let c = 0; c < components; ++c) {
      incoming.push(times[key] - 0.15, values[key * components + c] - 0.7);
      outgoing.push(times[key] + 0.2, values[key * components + c] + 0.9);
    }
    track.settings = { inTangents: new Float64Array(incoming), outTangents: new Float64Array(outgoing) };
    for (const ending of interpolation === THREE.InterpolateSmooth ? [0, 1, 2] : [0]) {
      const interpolant = track.createInterpolant(new Float64Array(components));
      interpolant.settings = { endingStart: THREE.ZeroCurvatureEnding + ending, endingEnd: THREE.ZeroCurvatureEnding + ending };
      // enum ordering is zero-curvature, zero-slope, wrap in the pinned package.
      const expected = samples.flatMap(time => Array.from(interpolant.evaluate(time)));
      assert(expected.every(Number.isFinite));
      tracks.push({name: `${Class.name}/${interpolation}/${ending}`, times: Array.from(track.times), values: Array.from(track.values), incoming, outgoing,
        keys: times.length, components, kind, interpolation: interpolation - THREE.InterpolateDiscrete, ending, samples, expected, tangents: true});
    }
  }
}
for (const interpolation of [THREE.InterpolateDiscrete, THREE.InterpolateLinear]) {
  const track = new THREE.QuaternionKeyframeTrack('.quaternion', [0, 1, 2], [0, 0, 0, 1, 0, 0, -Math.SQRT1_2, -Math.SQRT1_2, 0, 0, 1, 0], interpolation);
  const interpolant = track.createInterpolant(new Float64Array(4));
  tracks.push({name: `quaternion/${interpolation}`, times: Array.from(track.times), values: Array.from(track.values), incoming: [], outgoing: [], keys: 3,
    components: 4, kind: 3, interpolation: interpolation - THREE.InterpolateDiscrete, ending: 0, samples, expected: samples.flatMap(t => Array.from(interpolant.evaluate(t))), tangents: false});
}
for (const interpolation of [THREE.InterpolateDiscrete, THREE.InterpolateLinear]) {
  const track = new THREE.NumberKeyframeTrack('.value', [0, 0.5, 0.5, 2], [0, 3, 7, 4], interpolation);
  const interpolant = track.createInterpolant(new Float64Array(1));
  tracks.push({name: `equal-times/${interpolation}`, times: Array.from(track.times), values: Array.from(track.values), incoming: [], outgoing: [], keys: 4,
    components: 1, kind: 0, interpolation: interpolation - THREE.InterpolateDiscrete, ending: 0, samples, expected: samples.flatMap(t => Array.from(interpolant.evaluate(t))), tangents: false});
}

// One operation table drives both engines, including every observable value after control calls.
const opNames = ['update', 'seek', 'play', 'stop', 'reset', 'weight', 'scale', 'loop', 'clamp', 'paused', 'enabled', 'fade', 'crossfade', 'warp', 'start', 'mixerScale', 'uncache'];
const scenarios = [
  ['repeat-reverse-seek', false, [ ['play',0], ['update',0,0.25], ['update',0,1.75], ['update',0,4.5], ['scale',0,-1], ['update',0,0.8], ['seek',0,0.6], ['stop',0] ]],
  ['once-clamp', false, [ ['loop',0,0,1], ['clamp',0,1], ['play',0], ['update',0,1.75], ['update',0,0.25], ['update',0,0.2], ['reset',0], ['scale',0,-1], ['update',0,0.1], ['stop',0] ]],
  ['finite-pingpong', false, [ ['loop',0,2,3], ['clamp',0,1], ['play',0], ['update',0,1.9], ['update',0,0.1], ['update',0,0.75], ['update',0,1.25], ['update',0,2], ['stop',0] ]],
  ['weighted-crossfade-warp', false, [ ['weight',0,0.3], ['weight',1,0.4], ['play',0], ['play',1], ['update',0,0.4], ['crossfade',0,1,1,1], ['update',0,0.25], ['update',0,0.5], ['update',0,0.25], ['update',0,0.1], ['stop',1], ['stop',0] ]],
  ['additive-quaternion', true, [ ['weight',0,0.25], ['weight',1,0.4], ['play',0], ['play',1], ['update',0,0.5], ['update',0,0.6], ['paused',0,1], ['fade',1,0.5,0], ['update',0,0.25], ['update',0,0.3], ['stop',1], ['stop',0] ]],
  ['schedule-warp-idle', false, [ ['start',0,0.5], ['play',0], ['update',0,0.2], ['update',0,0.5], ['warp',0,1,0,0.5], ['update',0,0.25], ['update',0,0.3], ['paused',0,0], ['scale',0,1], ['mixerScale',0,-1], ['update',0,0.4], ['uncache',0] ]],
];
const outputs = [];
for (const [name, additive, operations] of scenarios) {
  const root = new THREE.Object3D(); root.opacity = 7;
  root.quaternion.setFromAxisAngle(new THREE.Vector3(0,0,1), 0.5);
  const clips = [new THREE.AnimationClip('a', 2, [new THREE.NumberKeyframeTrack('.opacity',[0,1,2],[2,10,4]), new THREE.QuaternionKeyframeTrack('.quaternion',[0,1,2],[0,0,0,1, 0,Math.SQRT1_2,0,Math.SQRT1_2, 0,1,0,0])]),
    new THREE.AnimationClip('b', 1, [new THREE.NumberKeyframeTrack('.opacity',[0,0.5,1],[-3,5,9]), new THREE.QuaternionKeyframeTrack('.quaternion',[0,0.5,1],[0,0,0,1, Math.SQRT1_2,0,0,Math.SQRT1_2, 1,0,0,0])], additive ? THREE.AdditiveAnimationBlendMode : THREE.NormalAnimationBlendMode)];
  const mixer = new THREE.AnimationMixer(root), actions = clips.map(clip => mixer.clipAction(clip));
  const events=[]; mixer.addEventListener('loop', e=>events.push(`loop:${actions.indexOf(e.action)}:${e.loopDelta};`));
  mixer.addEventListener('finished', e=>events.push(`finished:${actions.indexOf(e.action)}:${e.direction};`));
  const steps=[];
  for (const [op, index=0, a=0, b=0, c=0] of operations) {
    const action = actions[index]; events.length=0;
    switch(op) {
      case 'update': mixer.update(a); break;
      case 'seek': mixer.setTime(a); break;
      case 'play': action.play(); break;
      case 'stop': action.stop(); break;
      case 'reset': action.reset(); break;
      case 'weight': action.weight=a; break;
      case 'scale': action.timeScale=a; break;
      case 'loop': action.setLoop([THREE.LoopOnce,THREE.LoopRepeat,THREE.LoopPingPong][a],b); break;
      case 'clamp': action.clampWhenFinished=!!a; break;
      case 'paused': action.paused=!!a; break;
      case 'enabled': action.enabled=!!a; break;
      case 'fade': b ? action.fadeIn(a) : action.fadeOut(a); break;
      case 'crossfade': action.crossFadeTo(actions[a],b,!!c); break;
      case 'warp': action.warp(a,b,c); break;
      case 'start': action.startAt(a); break;
      case 'mixerScale': mixer.timeScale=a; break;
      case 'uncache': mixer.uncacheAction(clips[index]); break;
      default: throw new Error(op);
    }
    steps.push({op:opNames.indexOf(op),index,a,b,c,values:[root.opacity,...root.quaternion.toArray()],time:mixer.time,events:events.join('')});
  }
  outputs.push({name,additive,steps});
}
const number = n => Number.isFinite(n) ? String(n) : (()=>{throw new Error('nonfinite oracle');})();
const array = a => `{${a.map(number).join(',')}}`;
let output = '// Generated by animation-oracle.mjs using unmodified Three.js 0.186.1. Do not edit.\n';
output += 'static const AnimationOracleTrack animation_oracle_tracks[] = {\n';
for (const t of tracks) output += `{${JSON.stringify(t.name)},${array(t.times)},${array(t.values)},${array(t.incoming)},${array(t.outgoing)},${t.keys},${t.components},${t.kind},${t.interpolation},${t.ending},${t.tangents},${array(t.samples)},${array(t.expected)},${t.samples.length}},\n`;
output += '};\n';
outputs.forEach((scenario,i)=> {
  output += `static const AnimationOracleStep animation_oracle_steps_${i}[] = {\n`;
  for (const s of scenario.steps) output += `{${s.op},${s.index},${s.a},${s.b},${s.c},${array(s.values)},${s.time},${JSON.stringify(s.events)}},\n`;
  output += '};\n';
});
output += 'static const AnimationOracleScenario animation_oracle_scenarios[] = {\n';
outputs.forEach((s,i)=> output += `{${JSON.stringify(s.name)},${s.additive},animation_oracle_steps_${i},${s.steps.length}},\n`);
output += '};\n';
writeFileSync(new URL('./animation-oracle.inc', import.meta.url), output);
console.log(`Three.js oracle: ${tracks.length} typed tracks and ${outputs.length} playback scenarios.`);
