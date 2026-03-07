// JetStream Benchmark: richards (Octane)
// OS kernel task scheduler simulation
// Original: Martin Richards (BCPL), V8 project authors
// Simulates task dispatching with idle, worker, handler, and device tasks

// Task IDs
let ID_IDLE      = 0
let ID_WORKER    = 1
let ID_HANDLER_A = 2
let ID_HANDLER_B = 3
let ID_DEVICE_A  = 4
let ID_DEVICE_B  = 5
let NUM_IDS      = 6

let KIND_DEVICE = 0
let KIND_WORK   = 1
let DATA_SIZE   = 4
let COUNT        = 1000

// Task states
let STATE_RUNNING           = 0
let STATE_RUNNABLE          = 1
let STATE_SUSPENDED         = 2
let STATE_HELD              = 4
let STATE_SUSPENDED_RUNNABLE = 3

// Task function IDs
let FN_IDLE    = 0
let FN_WORKER  = 1
let FN_HANDLER = 2
let FN_DEVICE  = 3

pn create_packet(link, id: int, kind: int) {
    var data = fill(4, 0)
    return {link: link, id: id, kind: kind, a1: 0, a2: data}
}

pn packet_add_to(packet, queue) {
    packet.link = null
    if (queue == null) {
        return packet
    }
    var next = queue
    var peek = next.link
    while (peek != null) {
        next = peek
        peek = next.link
    }
    next.link = packet
    return queue
}

pn create_tcb(link, id: int, priority: int, queue, state: int, fn_id: int) {
    return {link: link, id: id, priority: priority, queue: queue,
            state: state, fn_id: fn_id,
            v1: 0, v2: 0, work_in: null, dev_in: null}
}

pn tcb_is_held_or_suspended(tcb) {
    if (band(tcb.state, STATE_HELD) != 0) {
        return true
    }
    if (tcb.state == STATE_SUSPENDED) {
        return true
    }
    return false
}

pn create_scheduler() {
    return {queue_count: 0, hold_count: 0,
            task_list: null, current_tcb: null, current_id: 0}
}

pn scheduler_add_task(sched, blocks, id: int, pri: int, queue, state: int, fn_id: int) {
    var tcb = create_tcb(sched.task_list, id, pri, queue, state, fn_id)
    sched.task_list = tcb
    blocks[id] = tcb
    sched.current_tcb = tcb
}

pn scheduler_add_idle_task(sched, blocks, id: int, pri: int, queue, count: int) {
    scheduler_add_task(sched, blocks, id, pri, queue, STATE_RUNNABLE, FN_IDLE)
    var tcb = sched.current_tcb
    tcb.v1 = 1
    tcb.v2 = count
}

pn scheduler_add_worker_task(sched, blocks, id: int, pri: int, queue) {
    scheduler_add_task(sched, blocks, id, pri, queue, STATE_SUSPENDED_RUNNABLE, FN_WORKER)
    var tcb = sched.current_tcb
    tcb.v1 = ID_HANDLER_A
    tcb.v2 = 0
}

pn scheduler_add_handler_task(sched, blocks, id: int, pri: int, queue) {
    scheduler_add_task(sched, blocks, id, pri, queue, STATE_SUSPENDED_RUNNABLE, FN_HANDLER)
}

pn scheduler_add_device_task(sched, blocks, id: int, pri: int, queue) {
    scheduler_add_task(sched, blocks, id, pri, queue, STATE_SUSPENDED, FN_DEVICE)
}

pn scheduler_release(sched, blocks, id: int) {
    var tcb = blocks[id]
    if (tcb == null) {
        return tcb
    }
    tcb.state = band(tcb.state, bnot(STATE_HELD))
    var cur = sched.current_tcb
    if (tcb.priority > cur.priority) {
        return tcb
    }
    return cur
}

pn scheduler_hold_current(sched) {
    sched.hold_count = sched.hold_count + 1
    var tcb = sched.current_tcb
    tcb.state = bor(tcb.state, STATE_HELD)
    return tcb.link
}

pn scheduler_suspend_current(sched) {
    var tcb = sched.current_tcb
    tcb.state = bor(tcb.state, STATE_SUSPENDED)
    return tcb
}

pn scheduler_queue(sched, blocks, packet) {
    var t = blocks[packet.id]
    if (t == null) {
        return t
    }
    sched.queue_count = sched.queue_count + 1
    packet.link = null
    packet.id = sched.current_id
    var cur = sched.current_tcb
    if (t.queue == null) {
        t.queue = packet
        t.state = bor(t.state, STATE_RUNNABLE)
        if (t.priority > cur.priority) {
            return t
        }
    } else {
        t.queue = packet_add_to(packet, t.queue)
    }
    return cur
}

pn run_idle(sched, blocks, tcb, packet) {
    tcb.v2 = tcb.v2 - 1
    if (tcb.v2 == 0) {
        return scheduler_hold_current(sched)
    }
    if (band(tcb.v1, 1) == 0) {
        tcb.v1 = shr(tcb.v1, 1)
        return scheduler_release(sched, blocks, ID_DEVICE_A)
    } else {
        tcb.v1 = bxor(shr(tcb.v1, 1), 53256)
        return scheduler_release(sched, blocks, ID_DEVICE_B)
    }
}

pn run_worker(sched, blocks, tcb, packet) {
    if (packet == null) {
        return scheduler_suspend_current(sched)
    }
    if (tcb.v1 == ID_HANDLER_A) {
        tcb.v1 = ID_HANDLER_B
    } else {
        tcb.v1 = ID_HANDLER_A
    }
    packet.id = tcb.v1
    packet.a1 = 0
    var pkt_a2 = packet.a2
    var i: int = 0
    while (i < DATA_SIZE) {
        tcb.v2 = tcb.v2 + 1
        if (tcb.v2 > 26) {
            tcb.v2 = 1
        }
        pkt_a2[i] = tcb.v2
        i = i + 1
    }
    return scheduler_queue(sched, blocks, packet)
}

pn run_handler(sched, blocks, tcb, packet) {
    if (packet != null) {
        if (packet.kind == KIND_WORK) {
            tcb.work_in = packet_add_to(packet, tcb.work_in)
        } else {
            tcb.dev_in = packet_add_to(packet, tcb.dev_in)
        }
    }
    if (tcb.work_in != null) {
        var work = tcb.work_in
        var cnt = work.a1
        if (cnt < DATA_SIZE) {
            if (tcb.dev_in != null) {
                var dev = tcb.dev_in
                tcb.dev_in = dev.link
                var wa2 = work.a2
                dev.a1 = wa2[cnt]
                work.a1 = cnt + 1
                return scheduler_queue(sched, blocks, dev)
            }
        } else {
            tcb.work_in = work.link
            return scheduler_queue(sched, blocks, work)
        }
    }
    return scheduler_suspend_current(sched)
}

pn run_device(sched, blocks, tcb, packet) {
    if (packet == null) {
        if (tcb.dev_in == null) {
            return scheduler_suspend_current(sched)
        }
        var v = tcb.dev_in
        tcb.dev_in = null
        return scheduler_queue(sched, blocks, v)
    }
    tcb.dev_in = packet
    return scheduler_hold_current(sched)
}

pn run_task(sched, blocks, tcb, packet) {
    var fn_id = tcb.fn_id
    if (fn_id == FN_IDLE) {
        return run_idle(sched, blocks, tcb, packet)
    }
    if (fn_id == FN_WORKER) {
        return run_worker(sched, blocks, tcb, packet)
    }
    if (fn_id == FN_HANDLER) {
        return run_handler(sched, blocks, tcb, packet)
    }
    return run_device(sched, blocks, tcb, packet)
}

pn scheduler_schedule(sched, blocks) {
    sched.current_tcb = sched.task_list
    while (sched.current_tcb != null) {
        var tcb = sched.current_tcb
        if (tcb_is_held_or_suspended(tcb)) {
            sched.current_tcb = tcb.link
        } else {
            sched.current_id = tcb.id
            var packet = null
            if (tcb.state == STATE_SUSPENDED_RUNNABLE) {
                packet = tcb.queue
                tcb.queue = packet.link
                if (tcb.queue == null) {
                    tcb.state = STATE_RUNNING
                } else {
                    tcb.state = STATE_RUNNABLE
                }
            }
            sched.current_tcb = run_task(sched, blocks, tcb, packet)
        }
    }
}

pn run_richards() {
    var sched = create_scheduler()
    var blocks = [null, null, null, null, null, null]

    scheduler_add_idle_task(sched, blocks, ID_IDLE, 0, null, COUNT)

    var queue = create_packet(null, ID_WORKER, KIND_WORK)
    queue = create_packet(queue, ID_WORKER, KIND_WORK)
    scheduler_add_worker_task(sched, blocks, ID_WORKER, 1000, queue)

    queue = create_packet(null, ID_DEVICE_A, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_A, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_A, KIND_DEVICE)
    scheduler_add_handler_task(sched, blocks, ID_HANDLER_A, 2000, queue)

    queue = create_packet(null, ID_DEVICE_B, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_B, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_B, KIND_DEVICE)
    scheduler_add_handler_task(sched, blocks, ID_HANDLER_B, 3000, queue)

    scheduler_add_device_task(sched, blocks, ID_DEVICE_A, 4000, null)
    scheduler_add_device_task(sched, blocks, ID_DEVICE_B, 5000, null)

    scheduler_schedule(sched, blocks)

    if (sched.queue_count != 2322) {
        return false
    }
    if (sched.hold_count != 928) {
        return false
    }
    return true
}

pn main() {
    var __t0 = clock()
    var pass = true
    var iter: int = 0
    while (iter < 50) {
        if (run_richards() == false) {
            pass = false
        }
        iter = iter + 1
    }
    var __t1 = clock()
    if (pass) {
        print("richards: PASS\n")
    } else {
        print("richards: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
