// Offline only: execute pinned, unmodified cssDOOM functions behind mock host I/O.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const base = path.resolve(__dirname, '..');
const root = path.resolve(base, '../../..');
const source = path.resolve(process.argv[2] || path.join(root, 'temp/cssdoom-upstream'));
const pin = JSON.parse(fs.readFileSync(path.join(base, 'upstream.json'))).revision;
if (execFileSync('git', ['rev-parse', 'HEAD'], { cwd: source, encoding: 'utf8' }).trim() !== pin ||
    execFileSync('git', ['status', '--porcelain'], { cwd: source, encoding: 'utf8' }).trim()) {
  throw new Error('The oracle requires the clean pinned upstream checkout.');
}
let now = 0, nextTimer = 1, occupied = null, sectors = [], sounds = [], randoms = [];
const timers = new Map();
const math = Object.create(Math);
math.random = () => {
  if (!randoms.length) throw new Error('Unexpected upstream random draw');
  return randoms.shift();
};
const context = vm.createContext({ Math: math, performance: { now: () => now },
  setTimeout: (fn, ms) => { const id = nextTimer++; timers.set(id, { fn, at: now + ms }); return id; },
  clearTimeout: id => timers.delete(id) });
const files = new Map(), modules = new Map();
const renderer = { triggerFlash() {}, setPlayerDead() {}, removeProjectile() {},
  hidePowerup() {}, clearKeys() {}, buildDoor() {}, setDoorState() {} };
const mapData = { doors: [{ sectorIndex: 1, floorHeight: 0, closedHeight: 0,
  openHeight: 128, keyRequired: 'blue' }], walls: [] };
const mocks = new Map([
  ['src/audio/audio.js', { playSound: name => sounds.push(name) }],
  ['src/renderer/index.js', renderer],
  ['src/renderer/hud.js', { clearWeaponSlots() {} }],
  ['src/game/entities/weapons.js', { equipWeapon() {} }],
  ['src/game/spatial-grid.js', { forEachSectorAt: (_x, _y, fn) => sectors.forEach(fn) }],
  ['src/game/physics.js', { getSectorAt: () => occupied == null ? null : { sectorIndex: occupied } }],
  ['src/shared/maps.js', { mapData }]
]);
async function moduleAt(relative) {
  if (modules.has(relative)) return modules.get(relative);
  let mod;
  if (mocks.has(relative)) {
    const exports = mocks.get(relative);
    mod = new vm.SyntheticModule(Object.keys(exports), function () {
      for (const [key, value] of Object.entries(exports)) this.setExport(key, value);
    }, { context, identifier: relative });
  } else {
    const text = fs.readFileSync(path.join(source, relative), 'utf8');
    files.set(relative, crypto.createHash('sha256').update(text).digest('hex'));
    mod = new vm.SourceTextModule(text, { context, identifier: relative });
  }
  modules.set(relative, mod);
  await mod.link((specifier, parent) => moduleAt(path.posix.normalize(
    path.posix.join(path.posix.dirname(parent.identifier), specifier))));
  return mod;
}
function advance(ms) {
  while (true) {
    const next = [...timers].filter(([, entry]) => entry.at <= ms).sort((a, b) => a[1].at - b[1].at)[0];
    if (!next) break;
    now = next[1].at; timers.delete(next[0]); next[1].fn();
  }
  now = ms;
}
async function main() {
  const geometryModule = await moduleAt('src/game/geometry.js');
  const damageModule = await moduleAt('src/game/player/damage.js');
  const doorModule = await moduleAt('src/game/mechanics/doors.js');
  for (const mod of [geometryModule, damageModule, doorModule]) await mod.evaluate();
  const geo = geometryModule.namespace, damage = damageModule.namespace, doors = doorModule.namespace;
  const state = modules.get('src/game/state.js').namespace.state;
  const square = [{ x: -10, y: -10 }, { x: 10, y: -10 }, { x: 10, y: 10 }, { x: -10, y: 10 }];
  const geometry = [
    ...[[0, 0], [10, 0], [-10, 0], [11, 0]].map(point => ({ kind: 'polygon', point, polygon: square,
      expected: geo.pointInPolygon(...point, square) })),
    ...[[0, 0, 5, -10, 5, 10, 5], [0, 0, 5, -10, 4, 10, 4],
      [0, 0, 5, 3, 4, 3, 4], [0, 0, 5, 2, 4, 2, 4]].map(args => ({ kind: 'circle', args,
      expected: geo.circleLineCollision(...args) })),
    ...[[0, 0, 0, 1, -10, 5, 10, 5, 10], [0, 0, 0, 1, -10, 5, 10, 5, 5],
      [0, 0, 0, 1, -10, 0, 10, 0, 10], [0, 0, 0, 1, 0, 2, 0, 8, 10],
      [0, 0, 0, 1, 0, 5, 10, 5, 10]].map(args => ({ kind: 'ray', args,
      expected: geo.rayHitsSegment(...args) }))
  ];
  function reset(input) {
    sounds = []; now = input.time * 1000;
    Object.assign(state, { health: input.health, armor: input.armor, armorType: input.armor_type,
      skillLevel: input.skill, isDead: input.dead || false, deathTime: input.dead ? now : null,
      powerups: input.immune ? { invulnerability: 10 } : {}, sectorDamageTimer: 0,
      playerX: 0, playerY: 0, liftState: new Map() });
  }
  function result() { return { health: state.health, armor: state.armor, armor_type: state.armorType,
    dead: state.isDead, dead_at: state.deathTime == null ? null : state.deathTime / 1000, sounds }; }
  const damageCases = [
    { amount: 30 }, { amount: 30, armor: 30, armor_type: 1 },
    { amount: 30, armor: 30, armor_type: 2 }, { amount: 30, armor: 3, armor_type: 2 },
    { amount: 31, skill: 1, armor: 30, armor_type: 1 }, { amount: 1000 },
    { amount: 1000, immune: true }, { amount: 10, health: 0, dead: true }
  ].map(overrides => {
    const input = { amount: 0, health: 100, armor: 0, armor_type: 0, skill: 3, time: 2, ...overrides };
    reset(input); damage.damagePlayer(input.amount); return { input, expected: result() };
  });
  const hazards = [
    { special: 5, dt: 0.9 }, { special: 5, dt: 32 / 35 }, { special: 7, dt: 1 },
    { special: 5, dt: 1, suit: true }, { special: 16, dt: 1, suit: true, random: 0 },
    { special: 16, dt: 1, suit: true, random: 5 / 256 }
  ].map(input => {
    reset({ health: 100, armor: 0, armor_type: 0, skill: 3, time: 0 });
    state.powerups = input.suit ? { radsuit: 10 } : {};
    sectors = [{ sectorIndex: 0, floorHeight: 0, specialType: input.special, boundaries: [square] }];
    randoms = input.random == null ? [] : [input.random];
    damage.checkSectorDamage(input.dt);
    if (randoms.length) throw new Error('Unused upstream random input');
    return { input, expected: { ...result(), hazard_time: state.sectorDamageTimer } };
  });
  now = 0; sounds = []; state.collectedKeys = new Set(); doors.initDoors();
  const doorCases = [];
  function doorStep(label, at, action, inside = false) {
    occupied = inside ? 1 : null; advance(at * 1000);
    if (action === 'key') state.collectedKeys.add('blue');
    if (action === 'use' || action === 'key') doors.toggleDoor(1);
    const entry = state.doorState.get(1);
    doorCases.push({ label, at, action, inside, expected: { open: entry.open, passable: entry.passable,
      sounds: sounds.splice(0) } });
  }
  doorStep('locked', 0, 'use'); doorStep('unlock', 0, 'key');
  doorStep('before-clearance', 0.79, 'wait'); doorStep('clearance', 0.8, 'wait');
  doorStep('opened', 1, 'wait'); doorStep('extend', 3, 'use');
  doorStep('occupied', 7, 'wait', true); doorStep('close', 11, 'wait');
  const output = { commit: pin, source_files: Object.fromEntries(files),
    host: 'Virtual monotonic clock; recorded sound effects; explicit random stream; no renderer.',
    differences: ['Lambda door height is simulation-owned; open means opening or open.',
      'Lambda deadlines stop on pause; these inputs advance simulation time explicitly.',
      'Hazard RNG compares threshold outcomes, not the upstream Math.random bit stream.'],
    geometry, damage: damageCases, hazards, doors: doorCases };
  fs.writeFileSync(path.join(base, 'reference/upstream-oracle.json'), JSON.stringify(output, null, 2) + '\n');
  console.log(`Recorded ${geometry.length + damageCases.length + hazards.length + doorCases.length} upstream cases at ${pin}`);
}
main().catch(error => { console.error(error); process.exitCode = 1; });
