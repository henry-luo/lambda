// Read independently shipped TeX glyph baselines in DVI scaled points.
// Only standard DVI opcodes are accepted; TFM supplies character advances.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';

export function read_dvi(file) {
  const data = fs.readFileSync(file), fonts = new Map(), pages = [];
  let offset = 0, page, state, stack, font;
  const uint = (n) => { const v = data.readUIntBE(offset, n); offset += n; return v; };
  const sint = (n) => { const v = data.readIntBE(offset, n); offset += n; return v; };
  function glyph(slot, advance) {
    const f = fonts.get(font);
    assert.ok(f, `undefined DVI font ${font}`);
    page.glyphs.push({ slot, font: f.name, size: f.scale, x: state.h, y: state.v });
    if (advance) {
      const info = 6 + f.tfm.readUInt16BE(2) + slot - f.tfm.readUInt16BE(4);
      const table = 6 + f.tfm.readUInt16BE(2) + f.tfm.readUInt16BE(6) - f.tfm.readUInt16BE(4) + 1;
      const width = f.tfm.readInt32BE((table + f.tfm[info * 4]) * 4);
      state.h += Math.round(width * f.scale / 1048576);
    }
  }
  while (offset < data.length) {
    const op = uint(1);
    if (op < 128) glyph(op, true);
    else if (op <= 131) glyph(uint(op - 127), true);
    else if (op === 132 || op === 137) {
      const height = sint(4), width = sint(4);
      if (height > 0 && width > 0) page.rules.push({ x:state.h, y:state.v, width, height });
      if (op === 132) state.h += width;
    }
    else if (op <= 136) glyph(uint(op - 132), false);
    else if (op === 138) { /* nop */ }
    else if (op === 139) {
      offset += 44;
      state = { h: 0, v: 0, w: 0, x: 0, y: 0, z: 0 }; stack = [];
      page = { glyphs: [], rules: [], markers: [] }; pages.push(page);
    } else if (op === 140) assert.equal(stack.length, 0, 'balanced DVI page stack');
    else if (op === 141) stack.push({ ...state });
    else if (op === 142) { assert.ok(stack.length); state = stack.pop(); }
    else if (op <= 146) state.h += sint(op - 142);
    else if (op <= 151) { if (op !== 147) state.w = sint(op - 147); state.h += state.w; }
    else if (op <= 156) { if (op !== 152) state.x = sint(op - 152); state.h += state.x; }
    else if (op <= 160) state.v += sint(op - 156);
    else if (op <= 165) { if (op !== 161) state.y = sint(op - 161); state.v += state.y; }
    else if (op <= 170) { if (op !== 166) state.z = sint(op - 166); state.v += state.z; }
    else if (op <= 234) font = op - 171;
    else if (op <= 238) font = uint(op - 234);
    else if (op <= 242) {
      const length = uint(op - 238), value = data.toString('utf8', offset, offset + length); offset += length;
      page.markers.push({ value, x: state.h, y: state.v });
    } else if (op <= 246) {
      const id = uint(op - 242); uint(4);
      const scale = uint(4), design = uint(4), area = uint(1), length = uint(1);
      const name = data.toString('ascii', offset, offset + area + length); offset += area + length;
      const lookup = spawnSync('kpsewhich', [`${name}.tfm`], { encoding: 'utf8' });
      assert.equal(lookup.status, 0, `TFM for shipped DVI font ${name}`);
      const tfmPath = lookup.stdout.trim();
      fonts.set(id, { name, scale, design, tfmPath, tfm: fs.readFileSync(tfmPath) });
    } else if (op === 247) {
      assert.equal(uint(1), 2, 'standard DVI format');
      assert.equal(uint(4), 25400000); assert.equal(uint(4), 473628672); assert.equal(uint(4), 1000);
      const length = uint(1); offset += length;
    } else if (op === 248) break;
    else assert.fail(`unsupported DVI opcode ${op}`);
  }
  return { pages, fonts: [...fonts.values()] };
}
