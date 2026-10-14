import obj: .objects
import ops: .operators
import gfx: .graphics
import images: .images

let DEFAULTS = {max_steps: 10000000, max_stack: 8192, max_execution_depth: 1024,
    max_dictionary_depth: 128, max_graphics_depth: 128, max_clip_depth: 128, max_objects: 16384,
    max_composite_length: 1048576, max_token_bytes: 1048576, max_procedure_depth: 128, max_scan_objects: 65536,
    max_path_segments: 100000, max_draws: 50000, max_image_pixels: 16777216, max_decoded_bytes: 67108864}

pub fn limits(options) map^ {
    let supplied = if (options == null) {} else options
    let result = {*: DEFAULTS, *: supplied}
    let checked = [for (key, default_value in DEFAULTS)
        if (result[key] is int and result[key] > 0) true
        else raise error("EPS invalid positive integer limit: " ++ key)]
    result
}

pn execute_value(var m, value) int^ {
    if (value is map and value.offset != null) { m.offset = value.offset }
    if (value is map and value.kind == "operator" and value.executable != false) { let applied = ops.dispatch(m, value.value)^ }
    else if (value is map and value.kind == "name" and value.executable) {
        let resolved = obj.lookup(m, value.value)^
        let scheduled = obj.schedule(m, resolved)^
    } else { obj.push_operand(m, value) }
    return 0
}

pub pn run(doc, options) map^ {
    let checked = obj.require({offset: 0}, doc is map and doc.format == "eps" and doc.schema_version == 1, "invalid parsed EPS document")^
    let budget = limits(if (options == null) null else options.limits)^
    var system = {}
    for (key, signature in ops.SIGNATURES) { let operator_name = string(key); system[operator_name] = {kind: "operator", value: operator_name, executable: true} }
    var m = {stack: [], dicts: [obj.reference(0), obj.reference(1)], exec: [], source: null, images: [], text_metrics: {}, decoded_bytes: 0,
        heap: [{kind: "dict", value: system, access: "readonly"}, {kind: "dict", value: {}, access: "unlimited"}],
        graphics: gfx.initial(), gstack: [], saves: [], fonts: {}, draws: [], definitions: [], offset: doc.program.offset, steps: 0,
        limits: {*: budget, source_end: doc.program.offset + doc.program.length}}
    m.source = obj.allocate(m, "file", {source: doc.source, offset: doc.program.offset, end: doc.program.offset + doc.program.length})^
    push(m.exec, {kind: "source"})
    while (len(m.exec) > 0) {
        let allowed = obj.require(m, m.steps < budget.max_steps and len(m.stack) <= budget.max_stack and
            len(m.exec) <= budget.max_execution_depth and len(m.dicts) <= budget.max_dictionary_depth and
            len(m.gstack) <= budget.max_graphics_depth and len(m.graphics.path.segments) <= budget.max_path_segments and
            len(m.draws) <= budget.max_draws and len(m.definitions) <= budget.max_draws, "limitcheck: execution budget")^
        m.steps = m.steps + 1
        let index = len(m.exec) - 1
        let frame = m.exec[index]
        m.exec = slice(m.exec, 0, index)
        if (frame.kind == "composite") { let executed = execute_value(m, frame.value)^ }
        else if (frame.kind == "source") {
            let source = obj.values(m, m.source)^
            if (source.closed != true and source.offset < source.end) {
                let token = ps_scan(source.source, source.offset, m.limits)
                let valid = obj.require(m, token is map, "invalid source scanner request")^
                m.offset = token.offset
                if (token.kind == "error") raise obj.failure(m, "syntaxerror: " ++ token.message)
                if (token.kind != "eof") {
                    let progress = obj.require(m, token.next > source.offset and token.next <= source.end, "syntaxerror: invalid source boundary")^
                    m.heap[m.source.id].value.offset = token.next
                    push(m.exec, frame)
                    let value = obj.materialize(m, token)^
                    let executed = execute_value(m, value)^
                }
            }
        } else if (frame.kind == "image") { let resumed = images.resume(m, frame)^ }
        else if (frame.kind == "procedure") {
            let body = obj.values(m, frame.value, true)^
            if (frame.index < len(body)) {
                push(m.exec, {*: frame, index: frame.index + 1})
                let executed = execute_value(m, body[frame.index])^
            }
        } else if (frame.kind == "repeat") {
            if (frame.remaining != 0) {
                push(m.exec, {*: frame, remaining: if (frame.remaining < 0) -1 else frame.remaining - 1})
                let scheduled = obj.schedule(m, frame.value)^
            }
        } else if (frame.kind == "for") {
            if ((obj.number_value(frame.step) >= 0 and obj.number_value(frame.current) <= obj.number_value(frame.limit)) or
                (obj.number_value(frame.step) < 0 and obj.number_value(frame.current) >= obj.number_value(frame.limit))) {
                push(m.exec, {*: frame, current: ops.numeric(m, "add", [frame.current, frame.step])^})
                obj.push_operand(m, frame.current)
                let scheduled = obj.schedule(m, frame.value)^
            }
        } else if (frame.kind == "forall") {
            let data = obj.values(m, frame.composite)^
            let n = if (frame.keys == null) len(data) else len(frame.keys)
            if (frame.index < n) {
                push(m.exec, {*: frame, index: frame.index + 1})
                if (frame.keys != null) {
                    let key = frame.keys[frame.index]
                    obj.push_operand(m, obj.literal_name(key)); obj.push_operand(m, data[key])
                } else { obj.push_operand(m, data[frame.index]) }
                let scheduled = obj.schedule(m, frame.value)^
            }
        } else raise obj.failure(m, "invalid execution frame")
    }
    let box = if (doc.hires_bbox != null) doc.hires_bbox else doc.bbox
    let geometry = obj.require(m, box is array and len(box) == 4 and not doc.bbox_pending, "unresolved bounding box")^
    return {draws: m.draws, definitions: m.definitions, bbox: box, metadata: doc.metadata, diagnostics: doc.diagnostics,
        stats: {steps: m.steps, objects: len(m.heap), draws: len(m.draws)}, stack: m.stack, objects: m.heap}
}
