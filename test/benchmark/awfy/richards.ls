// AWFY Benchmark: Richards — COW-safe handle-store port
// OS kernel task scheduler simulation
// Result: PASS when qpc=2322 and hc=928
//
// The pointer-oriented reference scheduler keeps one TCB record
// reached from `sched.tl`, from `task_table[identity]`, and from `sched.ct`,
// and every task function mutates whichever handle it happens to hold. That
// shape depends on aliasing, which S9.1.2/S9.3.1 rule out — values never alias,
// so those three would be three independent copies drifting apart.
//
// This port applies the handle-store idiom (C4.2e; doc/Lambda_Procedural.md
// "Sharing Mutable State"): every record has exactly one owner, and every field
// that used to hold a pointer holds an id into that owner instead.
//
//   w.tasks[id]  owns every TCB          — `link`, `ct`, `tl` are task ids
//   w.datas[id]  owns each task's data   — indexed by the owning task's id
//   w.pkts[id]   owns every Packet       — `link`, `input`, queue heads are packet ids
//
// `w` is the single mutation root and travels as one `var` parameter, so
// S9.1.3's exclusivity check is trivially satisfied at every call site.
// NONE (-1) replaces the null pointer.

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
let NUM_PACKETS = 8      // the benchmark allocates 8 packets, then recirculates them

// Task function IDs
let FN_IDLE    = 0
let FN_WORKER  = 1
let FN_HANDLER = 2
let FN_DEVICE  = 3

let NONE = -1            // the handle-store spelling of a null pointer

// --- Packet: owned by w.pkts, addressed by slot id ---

pn create_packet(var w, link, identity, pkind) {
    var pid = w.np
    w.np = pid + 1
    w.pkts[pid] = {link: link, identity: identity, pkind: pkind,
                   datum: 0, data: [0, 0, 0, 0]}
    return pid
}

// append packet to end of queue, return queue head id
pn append_packet(var w, pid, queue_head) {
    w.pkts[pid].link = NONE
    if (queue_head == NONE) {
        return pid
    }
    var mouse = queue_head
    var lnk = w.pkts[mouse].link
    while (lnk != NONE) {
        mouse = lnk
        lnk = w.pkts[mouse].link
    }
    w.pkts[mouse].link = pid
    return queue_head
}

// --- TaskControlBlock: owned by w.tasks, addressed by task identity ---

pn tcb_is_held_or_waiting(var w, tid) {
    var th = w.tasks[tid].th
    if (th == true) {
        return 1
    }
    var pp = w.tasks[tid].pp
    var tw = w.tasks[tid].tw
    if (pp == false) {
        if (tw == true) {
            return 1
        }
    }
    return 0
}

pn tcb_is_waiting_with_packet(var w, tid) {
    var pp = w.tasks[tid].pp
    var tw = w.tasks[tid].tw
    var th = w.tasks[tid].th
    if (pp == true) {
        if (tw == true) {
            if (th == false) {
                return 1
            }
        }
    }
    return 0
}

pn tcb_set_running(var w, tid) {
    w.tasks[tid].pp = false
    w.tasks[tid].tw = false
    w.tasks[tid].th = false
}

pn tcb_set_packet_pending(var w, tid) {
    w.tasks[tid].pp = true
    w.tasks[tid].tw = false
    w.tasks[tid].th = false
}

pn tcb_add_input(var w, tid, pid, old_task) {
    var inp = w.tasks[tid].input
    if (inp == NONE) {
        w.tasks[tid].input = pid
        w.tasks[tid].pp = true
        var tp = w.tasks[tid].priority
        var op = w.tasks[old_task].priority
        if (tp > op) {
            return tid
        }
        return old_task
    }
    var new_input = append_packet(w, pid, inp)
    w.tasks[tid].input = new_input
    return old_task
}

pn tcb_run_task(var w, tid) {
    var message = NONE
    var ww = tcb_is_waiting_with_packet(w, tid)
    if (ww == 1) {
        message = w.tasks[tid].input
        var msg_link = w.pkts[message].link
        w.tasks[tid].input = msg_link
        var inp2 = w.tasks[tid].input
        if (inp2 == NONE) {
            tcb_set_running(w, tid)
        }
        if (inp2 != NONE) {
            tcb_set_packet_pending(w, tid)
        }
    }
    // the running task's data record is w.datas[tid]; tid is also w.ct here
    var fid = w.tasks[tid].fn_id
    if (fid == 0) {
        return task_fn_idle(w, message, tid)
    }
    if (fid == 1) {
        return task_fn_worker(w, message, tid)
    }
    if (fid == 2) {
        return task_fn_handler(w, message, tid)
    }
    return task_fn_device(w, message, tid)
}

// --- Scheduler helpers ---

pn hold_self(var w, ct) {
    var hc = w.hc + 1
    w.hc = hc
    w.tasks[ct].th = true
    var lnk = w.tasks[ct].link
    return lnk
}

pn mark_waiting(var w, ct) {
    w.tasks[ct].tw = true
    return ct
}

pn queue_packet(var w, pid, ct) {
    var did = w.pkts[pid].identity       // the packet's destination task
    if (did < 0) {
        return NONE
    }
    var qpc = w.qpc + 1
    w.qpc = qpc
    w.pkts[pid].link = NONE
    var cti = w.cti
    w.pkts[pid].identity = cti           // identity is reused as the sender id
    var result = tcb_add_input(w, did, pid, ct)
    return result
}

pn release_task(var w, tid, ct) {
    if (tid < 0) {
        return NONE
    }
    w.tasks[tid].th = false
    var tp = w.tasks[tid].priority
    var cp = w.tasks[ct].priority
    if (tp > cp) {
        return tid
    }
    return ct
}

// --- Task functions ---

pn task_fn_idle(var w, work, tid) {
    var ct = w.ct
    var ic = w.datas[tid].icount - 1
    w.datas[tid].icount = ic
    if (ic == 0) {
        return hold_self(w, ct)
    }
    var ctrl = w.datas[tid].control
    var r = band(ctrl, 1)
    if (r == 0) {
        var nctrl = shr(ctrl, 1)
        w.datas[tid].control = nctrl
        return release_task(w, DEVICE_A, ct)
    }
    var nctrl2 = bxor(shr(ctrl, 1), 0xD008)
    w.datas[tid].control = nctrl2
    return release_task(w, DEVICE_B, ct)
}

pn task_fn_worker(var w, work, tid) {
    var ct = w.ct
    if (work == NONE) {
        return mark_waiting(w, ct)
    }
    var dest = w.datas[tid].destination
    if (dest == HANDLER_A) {
        w.datas[tid].destination = HANDLER_B
    }
    if (dest != HANDLER_A) {
        w.datas[tid].destination = HANDLER_A
    }
    var ndest = w.datas[tid].destination

    // read-modify-write: mutate the packet once, then put it back, so the
    // in-loop writes land on the owner rather than on a detached copy
    var p = w.pkts[work]
    p.identity = ndest
    p.datum = 0
    var i = 0
    while (i < DATA_SIZE) {
        var wc = w.datas[tid].wcount + 1
        w.datas[tid].wcount = wc
        if (wc > 26) {
            w.datas[tid].wcount = 1
            wc = 1
        }
        var ch = 64 + wc
        p.data[i] = ch
        i = i + 1
    }
    w.pkts[work] = p
    return queue_packet(w, work, ct)
}

pn task_fn_handler(var w, work, tid) {
    var ct = w.ct
    if (work != NONE) {
        var wk = w.pkts[work].pkind
        if (wk == WORK_PACKET_KIND) {
            var wi = w.datas[tid].work_in
            var nwi = append_packet(w, work, wi)
            w.datas[tid].work_in = nwi
        }
        if (wk == DEVICE_PACKET_KIND) {
            var di = w.datas[tid].device_in
            var ndi = append_packet(w, work, di)
            w.datas[tid].device_in = ndi
        }
    }
    var work_pkt = w.datas[tid].work_in
    if (work_pkt == NONE) {
        return mark_waiting(w, ct)
    }
    var cnt = w.pkts[work_pkt].datum
    if (cnt >= DATA_SIZE) {
        var wl = w.pkts[work_pkt].link
        w.datas[tid].work_in = wl
        return queue_packet(w, work_pkt, ct)
    }
    var dev_pkt = w.datas[tid].device_in
    if (dev_pkt == NONE) {
        return mark_waiting(w, ct)
    }
    var dl = w.pkts[dev_pkt].link
    w.datas[tid].device_in = dl
    var dval = w.pkts[work_pkt].data[cnt]
    w.pkts[dev_pkt].datum = dval
    var nc = cnt + 1
    w.pkts[work_pkt].datum = nc
    return queue_packet(w, dev_pkt, ct)
}

pn task_fn_device(var w, work, tid) {
    var ct = w.ct
    if (work == NONE) {
        var pend = w.datas[tid].pending
        if (pend == NONE) {
            return mark_waiting(w, ct)
        }
        var fw = pend
        w.datas[tid].pending = NONE
        return queue_packet(w, fw, ct)
    }
    w.datas[tid].pending = work
    return hold_self(w, ct)
}

// --- Scheduler ---

// the aliasing original wrote `sched.tl = tcb; task_table[identity] = tcb`,
// giving two holders of one record; here the store holds the only TCB and the
// task list holds its id
pn create_task(var w, identity, priority, work,
               state_pp, state_tw, state_th, fn_id) {
    var tl = w.tl
    w.tasks[identity] = {link: tl, identity: identity, priority: priority,
                         input: work, pp: state_pp, tw: state_tw, th: state_th,
                         fn_id: fn_id}
    w.tl = identity
}

pn schedule(var w) {
    var ct = w.tl
    w.ct = ct
    while (ct != NONE) {
        var how = tcb_is_held_or_waiting(w, ct)
        if (how == 1) {
            var nxt = w.tasks[ct].link
            ct = nxt
            w.ct = ct
        }
        if (how == 0) {
            var cid = w.tasks[ct].identity
            w.cti = cid
            w.ct = ct
            ct = tcb_run_task(w, ct)
            w.ct = ct
        }
    }
}

pn benchmark() {
    // one world value: the single owner of every task, data record, and packet
    var w = {qpc: 0, hc: 0, ct: NONE, cti: 0, tl: NONE, np: 0,
             tasks: fill(6, null), datas: fill(6, null), pkts: fill(8, null)}

    // createIdler(IDLER, 0, null, createRunning) — pp=false, tw=false, th=false
    create_task(w, IDLER, 0, NONE, false, false, false, FN_IDLE)
    w.datas[IDLER] = {control: 1, icount: 1000}

    // createWorker(WORKER, 1000, workQ, createWaitingWithPacket)
    var workq = create_packet(w, NONE, WORKER, WORK_PACKET_KIND)
    workq = create_packet(w, workq, WORKER, WORK_PACKET_KIND)
    create_task(w, WORKER, 1000, workq, true, true, false, FN_WORKER)
    w.datas[WORKER] = {destination: HANDLER_A, wcount: 0}

    // createHandler(HANDLER_A, 2000, workQ, createWaitingWithPacket)
    workq = create_packet(w, NONE, DEVICE_A, DEVICE_PACKET_KIND)
    workq = create_packet(w, workq, DEVICE_A, DEVICE_PACKET_KIND)
    workq = create_packet(w, workq, DEVICE_A, DEVICE_PACKET_KIND)
    create_task(w, HANDLER_A, 2000, workq, true, true, false, FN_HANDLER)
    w.datas[HANDLER_A] = {work_in: NONE, device_in: NONE}

    // createHandler(HANDLER_B, 3000, workQ, createWaitingWithPacket)
    workq = create_packet(w, NONE, DEVICE_B, DEVICE_PACKET_KIND)
    workq = create_packet(w, workq, DEVICE_B, DEVICE_PACKET_KIND)
    workq = create_packet(w, workq, DEVICE_B, DEVICE_PACKET_KIND)
    create_task(w, HANDLER_B, 3000, workq, true, true, false, FN_HANDLER)
    w.datas[HANDLER_B] = {work_in: NONE, device_in: NONE}

    // createDevice(DEVICE_A, 4000, null, createWaiting) — pp=false, tw=true, th=false
    create_task(w, DEVICE_A, 4000, NONE, false, true, false, FN_DEVICE)
    w.datas[DEVICE_A] = {pending: NONE}

    // createDevice(DEVICE_B, 5000, null, createWaiting)
    create_task(w, DEVICE_B, 5000, NONE, false, true, false, FN_DEVICE)
    w.datas[DEVICE_B] = {pending: NONE}

    schedule(w)

    var qpc = w.qpc
    var hc = w.hc
    if (qpc == 2322) {
        if (hc == 928) {
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
    var __t0 = clock()
    var pass = true
    var k = 0
    while (k < 50) {
        var result = benchmark()
        if (result == 0) {
            pass = false
        }
        k = k + 1
    }
    var __t1 = clock()
    if (pass) {
        print("Richards: PASS\n")
    } else {
        print("Richards: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
