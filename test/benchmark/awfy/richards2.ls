// AWFY Benchmark: Richards
// OS kernel task scheduler simulation
// Ported from JavaScript AWFY suite
// Result: PASS when queuePacketCount=23246 and holdCount=9297

// Constants
let IDLER     = 0
let WORKER    = 1
let HANDLER_A = 2
let HANDLER_B = 3
let DEVICE_A  = 4
let DEVICE_B  = 5
let NUM_TYPES = 6

let DEVICE_PACKET_KIND = 0
let WORK_PACKET_KIND   = 1

let DATA_SIZE = 4

// Task function IDs
let FN_IDLE    = 0
let FN_WORKER  = 1
let FN_HANDLER = 2
let FN_DEVICE  = 3

// --- Packet ---
// { link: null, identity: 0, pkind: 0, datum: 0, data: [0,0,0,0] }

pn create_packet(link, identity, pkind) {
    var pkt = { link: null, identity: 0, pkind: 0, datum: 0, data: [0, 0, 0, 0] }
    pkt.link = link
    pkt.identity = identity
    pkt.pkind = pkind
    return pkt
}

// append packet to end of queue, return queue head
pn append_packet(packet, queue_head) {
    packet.link = null
    if (queue_head == null) {
        return packet
    }
    var mouse = queue_head
    var lnk = (mouse.link)
    while (lnk != null) {
        mouse = lnk
        lnk = (mouse.link)
    }
    mouse.link = packet
    return queue_head
}

// --- TaskControlBlock ---
// { link: null, identity: 0, priority: 0, input: null,
//   pp: false, tw: false, th: false,
//   handle: null, fn_id: 0 }

pn create_tcb(link, identity, priority, initial_work, state_pp, state_tw, state_th, handle, fn_id) {
    var tcb = { link: null, identity: 0, priority: 0, input: null,
                pp: false, tw: false, th: false,
                handle: null, fn_id: 0 }
    tcb.link = link
    tcb.identity = identity
    tcb.priority = priority
    tcb.input = initial_work
    tcb.pp = state_pp
    tcb.tw = state_tw
    tcb.th = state_th
    tcb.handle = handle
    tcb.fn_id = fn_id
    return tcb
}

pn tcb_is_held_or_waiting(tcb) {
    var th = (tcb.th)
    if (th == true) {
        return 1
    }
    var pp = (tcb.pp)
    var tw = (tcb.tw)
    if (pp == false) {
        if (tw == true) {
            return 1
        }
    }
    return 0
}

pn tcb_is_waiting_with_packet(tcb) {
    var pp = (tcb.pp)
    var tw = (tcb.tw)
    var th = (tcb.th)
    if (pp == true) {
        if (tw == true) {
            if (th == false) {
                return 1
            }
        }
    }
    return 0
}

pn tcb_set_running(tcb) {
    tcb.pp = false
    tcb.tw = false
    tcb.th = false
}

pn tcb_set_packet_pending(tcb) {
    tcb.pp = true
    tcb.tw = false
    tcb.th = false
}

pn tcb_set_waiting(tcb) {
    tcb.pp = false
    tcb.tw = true
    tcb.th = false
}

pn tcb_set_waiting_with_packet(tcb) {
    tcb.pp = true
    tcb.tw = true
    tcb.th = false
}

pn tcb_add_input(tcb, packet, old_task) {
    var inp = (tcb.input)
    if (inp == null) {
        tcb.input = packet
        tcb.pp = true
        var tp = (tcb.priority)
        var op = (old_task.priority)
        if (tp > op) {
            return tcb
        }
        return old_task
    }
    var new_input = append_packet(packet, inp)
    tcb.input = new_input
    return old_task
}

pn tcb_run_task(tcb, sched, task_table) {
    var message = null
    var ww = tcb_is_waiting_with_packet(tcb)
    if (ww == 1) {
        message = (tcb.input)
        var msg_link = (message.link)
        tcb.input = msg_link
        var inp2 = (tcb.input)
        if (inp2 == null) {
            tcb_set_running(tcb)
        }
        if (inp2 != null) {
            tcb_set_packet_pending(tcb)
        }
    }
    var fid = (tcb.fn_id)
    var hnd = (tcb.handle)
    if (fid == 0) {
        return task_fn_idle(message, hnd, sched, task_table)
    }
    if (fid == 1) {
        return task_fn_worker(message, hnd, sched, task_table)
    }
    if (fid == 2) {
        return task_fn_handler(message, hnd, sched, task_table)
    }
    return task_fn_device(message, hnd, sched, task_table)
}

// --- Data Records ---

pn create_device_data() {
    var rec = { pending: null }
    return rec
}

pn create_handler_data() {
    var rec = { work_in: null, device_in: null }
    return rec
}

pn create_idle_data() {
    var rec = { control: 1, icount: 10000 }
    return rec
}

pn create_worker_data() {
    var rec = { destination: 2, wcount: 0 }
    return rec
}

// --- Scheduler helpers ---

pn find_task(task_table, identity) {
    var t = task_table[identity]
    return t
}

pn hold_self(sched, current_task) {
    var hc = (sched.hc) + 1
    sched.hc = hc
    current_task.th = true
    var lnk = (current_task.link)
    return lnk
}

pn mark_waiting(current_task) {
    current_task.tw = true
    return current_task
}

pn queue_packet(sched, task_table, packet, current_task) {
    var pid = (packet.identity)
    var t = find_task(task_table, pid)
    if (t == null) {
        return null
    }
    var qpc = (sched.qpc) + 1
    sched.qpc = qpc
    packet.link = null
    var cti = (sched.cti)
    packet.identity = cti
    var result = tcb_add_input(t, packet, current_task)
    return result
}

pn release_task(sched, task_table, identity, current_task) {
    var t = find_task(task_table, identity)
    if (t == null) {
        return null
    }
    t.th = false
    var tp = (t.priority)
    var cp = (current_task.priority)
    if (tp > cp) {
        return t
    }
    return current_task
}

// --- Task functions ---

pn task_fn_idle(work, data, sched, task_table) {
    var ct = (sched.ct)
    var ic = (data.icount) - 1
    data.icount = ic
    if (ic == 0) {
        return hold_self(sched, ct)
    }
    var ctrl = (data.control)
    var r = band(ctrl, 1)
    if (r == 0) {
        var nctrl = shr(ctrl, 1)
        data.control = nctrl
        return release_task(sched, task_table, DEVICE_A, ct)
    }
    var nctrl2 = bxor(shr(ctrl, 1), 53256)
    data.control = nctrl2
    return release_task(sched, task_table, DEVICE_B, ct)
}

pn task_fn_worker(work, data, sched, task_table) {
    var ct = (sched.ct)
    if (work == null) {
        return mark_waiting(ct)
    }
    var dest = (data.destination)
    // workaround: sole map assignment in if-block is dropped by transpiler
    // so add a temp var before it
    if (dest == HANDLER_A) {
        var _hb = HANDLER_B
        data.destination = _hb
    }
    if (dest != HANDLER_A) {
        var _ha = HANDLER_A
        data.destination = _ha
    }
    var ndest = (data.destination)
    work.identity = ndest
    work.datum = 0

    var wdata = (work.data)
    var i: int = 0
    while (i < DATA_SIZE) {
        var wc = (data.wcount) + 1
        data.wcount = wc
        if (wc > 26) {
            data.wcount = 1
            wc = 1
        }
        var ch = 64 + wc
        wdata[i] = ch
        i = i + 1
    }
    return queue_packet(sched, task_table, work, ct)
}

pn task_fn_handler(work, data, sched, task_table) {
    var ct = (sched.ct)
    if (work != null) {
        var wk = (work.pkind)
        if (wk == WORK_PACKET_KIND) {
            var wi = (data.work_in)
            var nwi = append_packet(work, wi)
            data.work_in = nwi
        }
        if (wk == DEVICE_PACKET_KIND) {
            var di = (data.device_in)
            var ndi = append_packet(work, di)
            data.device_in = ndi
        }
    }
    var work_pkt = (data.work_in)
    if (work_pkt == null) {
        return mark_waiting(ct)
    }
    var cnt = (work_pkt.datum)
    if (cnt >= DATA_SIZE) {
        var wl = (work_pkt.link)
        data.work_in = wl
        return queue_packet(sched, task_table, work_pkt, ct)
    }
    var dev_pkt = (data.device_in)
    if (dev_pkt == null) {
        return mark_waiting(ct)
    }
    var dl = (dev_pkt.link)
    data.device_in = dl
    var wd = (work_pkt.data)
    var dval = wd[cnt]
    dev_pkt.datum = dval
    var nc = cnt + 1
    work_pkt.datum = nc
    return queue_packet(sched, task_table, dev_pkt, ct)
}

pn task_fn_device(work, data, sched, task_table) {
    var ct = (sched.ct)
    if (work == null) {
        var pend = (data.pending)
        if (pend == null) {
            return mark_waiting(ct)
        }
        var fw = pend
        data.pending = null
        return queue_packet(sched, task_table, fw, ct)
    }
    data.pending = work
    return hold_self(sched, ct)
}

// --- Scheduler ---

pn create_task(sched, task_table, identity, priority, work, state_pp, state_tw, state_th, handle, fn_id) {
    var tl = (sched.tl)
    var tcb = create_tcb(tl, identity, priority, work, state_pp, state_tw, state_th, handle, fn_id)
    sched.tl = tcb
    task_table[identity] = tcb
}

pn schedule(sched, task_table) {
    var ct = (sched.tl)
    sched.ct = ct
    while (ct != null) {
        var how = tcb_is_held_or_waiting(ct)
        if (how == 1) {
            var nxt = (ct.link)
            ct = nxt
            sched.ct = ct
        }
        if (how == 0) {
            var cid = (ct.identity)
            sched.cti = cid
            sched.ct = ct
            ct = tcb_run_task(ct, sched, task_table)
            // After runTask, ct is the returned task (next to run)
            // update sched.ct for the next iteration
            sched.ct = ct
        }
    }
}

pn benchmark() {
    // Scheduler state
    var sched = { qpc: 0, hc: 0, ct: null, cti: 0, tl: null }
    var task_table = [null, null, null, null, null, null]

    // createIdler(IDLER, 0, null, createRunning)
    // createRunning: pp=false, tw=false, th=false
    var idle_data = create_idle_data()
    create_task(sched, task_table, IDLER, 0, null, false, false, false, idle_data, FN_IDLE)

    // createWorker(WORKER, 1000, workQ, createWaitingWithPacket)
    var workq = create_packet(null, WORKER, WORK_PACKET_KIND)
    workq = create_packet(workq, WORKER, WORK_PACKET_KIND)
    var worker_data = create_worker_data()
    // createWaitingWithPacket: pp=true, tw=true, th=false
    create_task(sched, task_table, WORKER, 1000, workq, true, true, false, worker_data, FN_WORKER)

    // createHandler(HANDLER_A, 2000, workQ, createWaitingWithPacket)
    workq = create_packet(null, DEVICE_A, DEVICE_PACKET_KIND)
    workq = create_packet(workq, DEVICE_A, DEVICE_PACKET_KIND)
    workq = create_packet(workq, DEVICE_A, DEVICE_PACKET_KIND)
    var handler_data_a = create_handler_data()
    create_task(sched, task_table, HANDLER_A, 2000, workq, true, true, false, handler_data_a, FN_HANDLER)

    // createHandler(HANDLER_B, 3000, workQ, createWaitingWithPacket)
    workq = create_packet(null, DEVICE_B, DEVICE_PACKET_KIND)
    workq = create_packet(workq, DEVICE_B, DEVICE_PACKET_KIND)
    workq = create_packet(workq, DEVICE_B, DEVICE_PACKET_KIND)
    var handler_data_b = create_handler_data()
    create_task(sched, task_table, HANDLER_B, 3000, workq, true, true, false, handler_data_b, FN_HANDLER)

    // createDevice(DEVICE_A, 4000, null, createWaiting)
    // createWaiting: pp=false, tw=true, th=false
    var device_data_a = create_device_data()
    create_task(sched, task_table, DEVICE_A, 4000, null, false, true, false, device_data_a, FN_DEVICE)

    // createDevice(DEVICE_B, 5000, null, createWaiting)
    var device_data_b = create_device_data()
    create_task(sched, task_table, DEVICE_B, 5000, null, false, true, false, device_data_b, FN_DEVICE)

    // Run scheduler
    schedule(sched, task_table)

    var qpc = (sched.qpc)
    var hc = (sched.hc)
    if (qpc == 23246) {
        if (hc == 9297) {
            return 1
        }
    }
    print("FAIL: qpc=")
    print(qpc)
    print(" hc=")
    print(hc)
    print("\n")
    return 0
}

pn main() {
    var result = benchmark()
    if (result == 1) {
        print("Richards: PASS\n")
    }
    if (result == 0) {
        print("Richards: FAIL\n")
    }
}
