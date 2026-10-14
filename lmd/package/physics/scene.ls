// S12.1.1v2/D7.5.3: live document mutation crosses the Radiant host boundary.
import radiant

pub pn play(node) { return radiant.physics(node, {operation: "play"}) }
pub pn pause(node) { return radiant.physics(node, {operation: "pause"}) }
pub pn reset(node) { return radiant.physics(node, {operation: "reset"}) }
pub pn step(node, ticks = 1) { return radiant.physics(node, {operation: "step", ticks: ticks}) }
pub pn snapshot(node) { return radiant.physics(node, {operation: "read"}) }
pub pn events(node) { return radiant.physics(node, {operation: "events"}) }
pub pn command(node, commands) { return radiant.physics(node, {operation: "apply", commands: commands}) }
pub pn query(node, queries) { return radiant.physics(node, {operation: "query", queries: queries}) }
