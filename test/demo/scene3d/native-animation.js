import * as THREE from './vendor/three/three.module.js';

// First-party binding adapter; all interpolation, action timing and blending run in the shared native engine.
const types = { number: 0, vector: 1, color: 2, quaternion: 3, bool: 4, string: 5 };
const interpolation = new Map([[THREE.InterpolateDiscrete, 0], [THREE.InterpolateLinear, 1],
    [THREE.InterpolateSmooth, 2], [THREE.InterpolateBezier, 3]]);
const propertyOwners = new WeakMap();
const fields = ['time', 'timeScale', 'weight', 'enabled', 'paused', 'clampWhenFinished',
    'zeroSlopeAtStart', 'zeroSlopeAtEnd'];

class NativeAnimationAction {
    constructor(mixer, clip, id) { this._mixer = mixer; this._clip = clip; this._id = id; }
    _call(op, a, b, c) {
        if (!this._id) throw new TypeError('Animation action has been uncached');
        return this._mixer._host.operate(8, this._id, op, a, b, c);
    }
    play() { this._call(0); return this; }
    stop() { this._call(1); return this; }
    reset() { this._call(2); return this; }
    fadeIn(duration) { this._call(3, duration); return this; }
    fadeOut(duration) { this._call(4, duration); return this; }
    warp(from, to, duration) { this._call(5, from, to, duration); return this; }
    crossFadeFrom(from, duration, warp = false) {
        if (from._mixer !== this._mixer) throw new TypeError('Crossfade actions must share a mixer');
        this._call(6, from._id, duration, Number(warp)); return this;
    }
    crossFadeTo(to, duration, warp = false) { to.crossFadeFrom(this, duration, warp); return this; }
    setLoop(mode, repetitions) { this._call(7, mode, repetitions); return this; }
    startAt(time) { this._call(8, time); return this; }
    setEffectiveWeight(weight) { this._call(9, weight); return this; }
    setEffectiveTimeScale(scale) { this._call(10, scale); return this; }
    stopFading() { this._call(11); return this; }
    stopWarping() { this._call(12); return this; }
    getEffectiveWeight() { return this._call(108); }
    getEffectiveTimeScale() { return this._call(109); }
    isScheduled() { return Boolean(this._call(110)); }
    isRunning() { return Boolean(this._call(111)); }
    getClip() { return this._clip; }
    getRoot() { return this._mixer._root; }
    getMixer() { return this._mixer; }
}
for (let i = 0; i < fields.length; i++) {
    const index = i;
    Object.defineProperty(NativeAnimationAction.prototype, fields[i], {
        get() { const value = this._call(100 + index); return index >= 3 ? Boolean(value) : value; },
        set(value) { this._call(20 + index, index >= 3 ? Number(Boolean(value)) : value); }
    });
}

export class NativeAnimationMixer extends THREE.EventDispatcher {
    constructor(root, canvas) {
        super();
        this._root = root;
        this._host = new RadiantAnimationHost(canvas);
        this._clips = new Map(); this._actions = new Map(); this._bindings = new Map(); this._nextClip = 1;
        this._scale = 1;
        this._host.operate(9, event => {
            event.action = this._actions.get(event.action);
            this.dispatchEvent(event);
        });
    }
    get time() { return this._host.operate(7); }
    get timeScale() { return this._scale; }
    set timeScale(value) { const scale = Number(value); this._host.operate(6, scale); this._scale = scale; }
    getRoot() { return this._root; }
    clipAction(clip, root = this._root, blendMode = clip.blendMode) {
        if (root !== this._root) throw new TypeError('Use a separate native mixer for another root');
        if (blendMode !== THREE.NormalAnimationBlendMode && blendMode !== THREE.AdditiveAnimationBlendMode)
            throw new TypeError('Unsupported animation blend mode');
        const existing = this._clips.get(clip);
        if (existing) {
            if (existing.blendMode !== blendMode) throw new TypeError('A native clip has one blend mode per mixer');
            return existing.action;
        }
        const acquired = [];
        try {
            const tracks = clip.tracks.map(track => {
                const type = types[track.ValueTypeName], count = track.getValueSize();
                const mode = interpolation.get(track.getInterpolation());
                if (type === undefined || mode === undefined || count < 1 || count > 16)
                    throw new TypeError('Unsupported native track type or interpolation');
                // Selected bindings cover TRS, visibility/name, camera/light/material values, bones and morph weights.
                const parsed = THREE.PropertyBinding.parseTrackName(track.name);
                const property = parsed.propertyName;
                const properties = ['position', 'quaternion', 'scale', 'visible', 'name', 'color', 'emissive',
                    'opacity', 'metalness', 'roughness', 'intensity', 'fov', 'near', 'far', 'aspect', 'morphTargetInfluences'];
                if (!properties.includes(property)) throw new TypeError('Unsupported native binding: ' + track.name);
                const indexed = parsed.propertyIndex !== undefined;
                const expected = property === 'visible' ? ['bool',1] : property === 'name' ? ['string',1] :
                    indexed ? ['number',1] : ['position','scale'].includes(property) ? ['vector',3] :
                    property === 'quaternion' ? ['quaternion',4] : ['color','emissive'].includes(property) ? ['color',3] :
                    property === 'morphTargetInfluences' ? ['number',count] : ['number',1];
                if(track.ValueTypeName!==expected[0]||count!==expected[1]) throw new TypeError('Invalid binding value type: '+track.name);
                let binding = this._bindings.get(track.name);
                if (!binding) {
                    const target = new THREE.PropertyBinding(root, track.name, parsed);
                    target.bind();
                    const buffer = new Array(count); target.getValue(buffer, 0);
                    if (buffer.some(value => value === undefined) || buffer.length !== count)
                        throw new TypeError('Unresolved native binding: ' + track.name);
                    const object = target.targetObject;
                    let owners = propertyOwners.get(object);
                    if (!owners) { owners = new Map(); propertyOwners.set(object, owners); }
                    // PropertyBinding resolves named morph indices; whole-property writes also overlap indexed writes.
                    const prefix = parsed.propertyName + ':', resolvedIndex = target.propertyIndex;
                    const key = prefix + (resolvedIndex ?? '');
                    if (owners.has(key) || owners.has(prefix) || (resolvedIndex === undefined &&
                        Array.from(owners.keys()).some(owned => owned.startsWith(prefix)))) {
                        target.unbind();
                        throw new TypeError('Animation property already has a native playback owner');
                    }
                    const mixer = this;
                    function attached() {
                        for (let node = target.node; node; node = node.parent) if (node === root) return;
                        mixer.dispose(); throw new TypeError('Animation target has been detached from its root');
                    }
                    const id = this._host.operate(0, {
                        type, count,
                        read() { attached(); target.getValue(buffer, 0); return buffer.slice(); },
                        write(values) {
                            attached(); target.setValue(values, 0);
                            if(object.isCamera && ['fov','near','far','aspect'].includes(property)) object.updateProjectionMatrix();
                        }
                    });
                    binding = { id, count, type, references: 0, target, owners, key };
                    owners.set(key, this);
                    this._bindings.set(track.name, binding);
                }
                if (binding.type !== type || binding.count !== count) throw new TypeError('Conflicting native binding types');
                binding.references++; acquired.push(track.name);
                return { property: binding.id, times: track.times, values: track.values, interpolation: mode,
                    inTangents: track.settings?.inTangents, outTangents: track.settings?.outTangents };
            });
            const id = this._host.operate(2, { id: this._nextClip++, duration: clip.duration,
                additive: blendMode === THREE.AdditiveAnimationBlendMode, tracks });
            const action = new NativeAnimationAction(this, clip, id);
            this._clips.set(clip, { action, blendMode, paths: acquired }); this._actions.set(id, action);
            return action;
        } catch (error) { this._releaseBindings(acquired); throw error; }
    }
    _releaseBindings(paths) {
        for (const path of paths) {
            const binding = this._bindings.get(path);
            if (--binding.references === 0) {
                this._host.operate(1, binding.id); binding.owners.delete(binding.key); binding.target.unbind(); this._bindings.delete(path);
            }
        }
    }
    existingAction(clip, root = this._root) { return root === this._root ? this._clips.get(clip)?.action ?? null : null; }
    update(delta) { this._host.operate(4, delta); return this; }
    setTime(time) { this._host.operate(5, time); return this; }
    setAutomatic(enabled = true) { this._host.operate(10, enabled); return this; }
    stopAllAction() { for (const action of this._actions.values()) action.stop(); return this; }
    uncacheAction(clip, root = this._root) {
        if (root !== this._root) return;
        const record = this._clips.get(clip); if (!record) return;
        this._host.operate(3, record.action._id); this._actions.delete(record.action._id);
        record.action._id = 0; this._clips.delete(clip); this._releaseBindings(record.paths);
    }
    uncacheClip(clip) { this.uncacheAction(clip); }
    uncacheRoot(root) { if (root === this._root) for (const clip of Array.from(this._clips.keys())) this.uncacheAction(clip); }
    dispose() {
        this._host.operate(11);
        for (const action of this._actions.values()) action._id = 0;
        for (const binding of this._bindings.values()) { binding.owners.delete(binding.key); binding.target.unbind(); }
        this._clips.clear(); this._actions.clear(); this._bindings.clear();
    }
}
