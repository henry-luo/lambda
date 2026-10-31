// S12.1.1v3/D7.5.3v2: live scene commands cross the existing Radiant host boundary.
import radiant

pub pn play(node) { return radiant.scene3d(node, {operation: "play"}) }
pub pn pause(node) { return radiant.scene3d(node, {operation: "pause"}) }
pub pn step(node, ticks = 1) { return radiant.scene3d(node, {operation: "step", ticks: ticks}) }
pub pn seek(node, seconds) { return radiant.scene3d(node, {operation: "seek", seconds: seconds}) }
pub pn reset(node) { return radiant.scene3d(node, {operation: "reset"}) }
pub pn snapshot(node) { return radiant.scene3d(node, {operation: "read"}) }
pub pn particle_snapshot(node) { return radiant.scene3d(node, {operation: "particle_read"}) }
pub pn particle_events(node, emitter = null, limit = 256) { return radiant.scene3d(node, {operation: "particle_events_read", emitter: emitter, limit: limit}) }
pub pn acknowledge_particle_events(node, emitter, through, session, generation) { return radiant.scene3d(node, {operation: "particle_events_ack", emitter: emitter, through: through, session: session, generation: generation}) }

pub pn animation_snapshot(node) { return radiant.scene3d(node, {operation: "animation_read"}) }
pub pn animation_commands(node, batch) { return radiant.scene3d(node, {operation: "animation_commands", batch: batch}) }
pub pn replay_animation_events(node, from, to, deliver = true) { return radiant.scene3d(node, {operation: "animation_replay_events", from: from, to: to, deliver: deliver}) }
pub pn time_scale(node, value) { return radiant.scene3d(node, {operation: "time_scale", value: value}) }
pub pn capabilities(node) { return radiant.scene3d(node, {operation: "capabilities"}) }
