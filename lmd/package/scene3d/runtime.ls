// S12.1.1v3/D7.5.3v2: live scene commands cross the existing Radiant host boundary.
import radiant

pub pn play(node) { return radiant.scene3d(node, {operation: "play"}) }
pub pn pause(node) { return radiant.scene3d(node, {operation: "pause"}) }
pub pn step(node, ticks = 1) { return radiant.scene3d(node, {operation: "step", ticks: ticks}) }
pub pn seek(node, seconds) { return radiant.scene3d(node, {operation: "seek", seconds: seconds}) }
pub pn reset(node) { return radiant.scene3d(node, {operation: "reset"}) }
pub pn snapshot(node) { return radiant.scene3d(node, {operation: "read"}) }
pub pn particle_snapshot(node) { return radiant.scene3d(node, {operation: "particle_read"}) }
pub pn particle_commands(node, batch) { return radiant.scene3d(node, {operation: "particle_commands", batch: batch}) }
pub pn particle_trail(node, emitter, index = 0) { return radiant.scene3d(node, {operation: "particle_trail_read", emitter: emitter, index: index}) }
pub pn particle_events(node, emitter = null, limit = 256) { return radiant.scene3d(node, {operation: "particle_events_read", emitter: emitter, limit: limit}) }
pub pn acknowledge_particle_events(node, emitter, through, session, generation) { return radiant.scene3d(node, {operation: "particle_events_ack", emitter: emitter, through: through, session: session, generation: generation}) }

pub pn animation_snapshot(node) { return radiant.scene3d(node, {operation: "animation_read"}) }
pub pn audio_snapshot(node) { return radiant.scene3d(node, {operation: "audio_read"}) }
pub pn audio_clip(node, request) { return radiant.scene3d(node, {operation: "audio_clip", payload: request}) }
pub pn audio_clip_drop(node, request) { return radiant.scene3d(node, {operation: "audio_clip_drop", payload: request}) }
pub pn audio_command(node, request) { return radiant.scene3d(node, {operation: "audio_command", payload: request}) }
pub pn audio_provider_read(node, request) { return radiant.scene3d(node, {operation: "audio_provider_read", payload: request}) }
pub pn audio_provider_route(node, request) { return radiant.scene3d(node, {operation: "audio_provider_route", payload: request}) }
pub pn audio_schedule(node, request) { return radiant.scene3d(node, {operation: "audio_schedule", payload: request}) }
pub pn audio_cues_cancel(node, request) { return radiant.scene3d(node, {operation: "audio_cues_cancel", payload: request}) }
pub pn audio_render_open(node, request) { return radiant.scene3d(node, {operation: "audio_render_open", payload: request}) }
pub pn audio_render_step(node, request) { return radiant.scene3d(node, {operation: "audio_render_step", payload: request}) }
pub pn audio_render_snapshot(node, request) { return radiant.scene3d(node, {operation: "audio_render_snapshot", payload: request}) }
pub pn audio_render_read(node, request) { return radiant.scene3d(node, {operation: "audio_render_read", payload: request}) }
pub pn audio_render_cancel(node, request) { return radiant.scene3d(node, {operation: "audio_render_cancel", payload: request}) }
pub pn audio_render_close(node, request) { return radiant.scene3d(node, {operation: "audio_render_close", payload: request}) }

// D7.5.4/S12.1.1v3: completed PCM streams through ordinary procedural IO authority.
pub pn audio_render_file(node, request, target) {
 let job=audio_render_snapshot(node,request)^
 if (job.state!="complete") { raise error("audio render: file output requires a completed job") }
 let header=audio_render_read(node,{session:request.session,job:request.job,format:"wav_header"})^
 output(header.data,target)^
 var offset=0
 let frames=int(job.frames)^
 while (offset<frames) {
  let count=min(4096,frames-offset)
  let chunk=audio_render_read(node,{session:request.session,job:request.job,offset:offset,frames:count})^
  output(chunk.data,target,{mode:"append"})^
  offset=offset+count
 }
 return job
}
pub pn animation_commands(node, batch) { return radiant.scene3d(node, {operation: "animation_commands", batch: batch}) }
pub pn replay_animation_events(node, from, to, deliver = true) { return radiant.scene3d(node, {operation: "animation_replay_events", from: from, to: to, deliver: deliver}) }
pub pn time_scale(node, value) { return radiant.scene3d(node, {operation: "time_scale", value: value}) }
pub pn capabilities(node) { return radiant.scene3d(node, {operation: "capabilities"}) }
