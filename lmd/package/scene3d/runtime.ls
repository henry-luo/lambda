// S12.1.1v3/D7.5.3v2: live scene commands cross the existing Radiant host boundary.
import radiant

pub pn play(node) { return radiant.scene3d(node, {operation: "play"}) }
pub pn pause(node) { return radiant.scene3d(node, {operation: "pause"}) }
pub pn step(node, ticks = 1) { return radiant.scene3d(node, {operation: "step", ticks: ticks}) }
pub pn seek(node, seconds) { return radiant.scene3d(node, {operation: "seek", seconds: seconds}) }
pub pn reset(node) { return radiant.scene3d(node, {operation: "reset"}) }
pub pn snapshot(node) { return radiant.scene3d(node, {operation: "read"}) }
pub pn time_scale(node, value) { return radiant.scene3d(node, {operation: "time_scale", value: value}) }
pub pn capabilities(node) { return radiant.scene3d(node, {operation: "capabilities"}) }
