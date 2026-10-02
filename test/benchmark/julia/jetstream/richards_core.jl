# port of test/benchmark/jetstream/richards.py; see ../LICENSE.md.
# JetStream Benchmark: richards (Octane) — Julia version
# OS kernel task scheduler simulation
# Original: Martin Richards (BCPL), V8 project authors
# Simulates task dispatching with idle, worker, handler, and device tasks
#
const ID_IDLE = 0
const ID_WORKER = 1
const ID_HANDLER_A = 2
const ID_HANDLER_B = 3
const ID_DEVICE_A = 4
const ID_DEVICE_B = 5
const NUM_IDS = 6
const KIND_DEVICE = 0
const KIND_WORK = 1
const DATA_SIZE = 4
const COUNT = 1000
const STATE_RUNNING = 0
const STATE_RUNNABLE = 1
const STATE_SUSPENDED = 2
const STATE_HELD = 4
const STATE_SUSPENDED_RUNNABLE = 3
const FN_IDLE = 0
const FN_WORKER = 1
const FN_HANDLER = 2
const FN_DEVICE = 3
function create_packet(link, pid, kind)
    return Dict{Any,Any}("link"=>link, "id"=>pid, "kind"=>kind, "a1"=>0, "a2"=>mul0([0], DATA_SIZE))
end

function packet_add_to(packet, queue)
    local nxt, peek
    set0!(packet, "link", nothing)
    if truth0(((queue === nothing)))
        return packet
    end
    nxt = queue
    peek = get0(nxt, "link")
    while truth0(((peek !== nothing)))
        nxt = peek
        peek = get0(nxt, "link")
    end
    set0!(nxt, "link", packet)
    return queue
end

function create_tcb(link, tid, priority, queue, state, fn_id)
    return Dict{Any,Any}("link"=>link, "id"=>tid, "priority"=>priority, "queue"=>queue, "state"=>state, "fn_id"=>fn_id, "v1"=>0, "v2"=>0, "work_in"=>nothing, "dev_in"=>nothing)
end

function tcb_is_held_or_suspended(tcb)
    return (let _bool_value = truth0((get0(tcb, "state") & STATE_HELD)); truth0(_bool_value) ? _bool_value : ((get0(tcb, "state") == STATE_SUSPENDED)) end)
end

function create_scheduler()
    return Dict{Any,Any}("queue_count"=>0, "hold_count"=>0, "task_list"=>nothing, "current_tcb"=>nothing, "current_id"=>0)
end

function scheduler_add_task(sched, blocks, tid, pri, queue, state, fn_id)
    local tcb
    tcb = create_tcb(get0(sched, "task_list"), tid, pri, queue, state, fn_id)
    set0!(sched, "task_list", tcb)
    set0!(blocks, tid, tcb)
    set0!(sched, "current_tcb", tcb)
    return nothing
end

function scheduler_add_idle_task(sched, blocks, tid, pri, queue, count)
    local tcb
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_RUNNABLE, FN_IDLE)
    tcb = get0(sched, "current_tcb")
    set0!(tcb, "v1", 1)
    set0!(tcb, "v2", count)
    return nothing
end

function scheduler_add_worker_task(sched, blocks, tid, pri, queue)
    local tcb
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_SUSPENDED_RUNNABLE, FN_WORKER)
    tcb = get0(sched, "current_tcb")
    set0!(tcb, "v1", ID_HANDLER_A)
    set0!(tcb, "v2", 0)
    return nothing
end

function scheduler_add_handler_task(sched, blocks, tid, pri, queue)
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_SUSPENDED_RUNNABLE, FN_HANDLER)
    return nothing
end

function scheduler_add_device_task(sched, blocks, tid, pri, queue)
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_SUSPENDED, FN_DEVICE)
    return nothing
end

function scheduler_release(sched, blocks, tid)
    local cur, tcb
    tcb = get0(blocks, tid)
    if truth0(((tcb === nothing)))
        return tcb
    end
    set0!(tcb, "state", (get0(tcb, "state") & ~(STATE_HELD)))
    cur = get0(sched, "current_tcb")
    if truth0(((get0(tcb, "priority") > get0(cur, "priority"))))
        return tcb
    end
    return cur
end

function scheduler_hold_current(sched)
    local tcb
    set0!(sched, "hold_count", add0(get0(sched, "hold_count"), 1))
    tcb = get0(sched, "current_tcb")
    set0!(tcb, "state", (get0(tcb, "state") | STATE_HELD))
    return get0(tcb, "link")
end

function scheduler_suspend_current(sched)
    local tcb
    tcb = get0(sched, "current_tcb")
    set0!(tcb, "state", (get0(tcb, "state") | STATE_SUSPENDED))
    return tcb
end

function scheduler_queue(sched, blocks, packet)
    local cur, t
    t = get0(blocks, get0(packet, "id"))
    if truth0(((t === nothing)))
        return t
    end
    set0!(sched, "queue_count", add0(get0(sched, "queue_count"), 1))
    set0!(packet, "link", nothing)
    set0!(packet, "id", get0(sched, "current_id"))
    cur = get0(sched, "current_tcb")
    if truth0(((get0(t, "queue") === nothing)))
        set0!(t, "queue", packet)
        set0!(t, "state", (get0(t, "state") | STATE_RUNNABLE))
        if truth0(((get0(t, "priority") > get0(cur, "priority"))))
            return t
        end
    else
        set0!(t, "queue", packet_add_to(packet, get0(t, "queue")))
    end
    return cur
end

function run_idle(sched, blocks, tcb, packet)
    set0!(tcb, "v2", (get0(tcb, "v2") - 1))
    if truth0(((get0(tcb, "v2") == 0)))
        return scheduler_hold_current(sched)
    end
    if truth0((((get0(tcb, "v1") & 1) == 0)))
        set0!(tcb, "v1", (get0(tcb, "v1") >> 1))
        return scheduler_release(sched, blocks, ID_DEVICE_A)
    else
        set0!(tcb, "v1", xor((get0(tcb, "v1") >> 1), 53256))
        return scheduler_release(sched, blocks, ID_DEVICE_B)
    end
    return nothing
end

function run_worker(sched, blocks, tcb, packet)
    local i, pkt_a2
    if truth0(((packet === nothing)))
        return scheduler_suspend_current(sched)
    end
    if truth0(((get0(tcb, "v1") == ID_HANDLER_A)))
        set0!(tcb, "v1", ID_HANDLER_B)
    else
        set0!(tcb, "v1", ID_HANDLER_A)
    end
    set0!(packet, "id", get0(tcb, "v1"))
    set0!(packet, "a1", 0)
    pkt_a2 = get0(packet, "a2")
    for i in range0(DATA_SIZE)
        set0!(tcb, "v2", add0(get0(tcb, "v2"), 1))
        if truth0(((get0(tcb, "v2") > 26)))
            set0!(tcb, "v2", 1)
        end
        set0!(pkt_a2, i, get0(tcb, "v2"))
    end
    return scheduler_queue(sched, blocks, packet)
end

function run_handler(sched, blocks, tcb, packet)
    local cnt, dev, wa2, work
    if truth0(((packet !== nothing)))
        if truth0(((get0(packet, "kind") == KIND_WORK)))
            set0!(tcb, "work_in", packet_add_to(packet, get0(tcb, "work_in")))
        else
            set0!(tcb, "dev_in", packet_add_to(packet, get0(tcb, "dev_in")))
        end
    end
    if truth0(((get0(tcb, "work_in") !== nothing)))
        work = get0(tcb, "work_in")
        cnt = get0(work, "a1")
        if truth0(((cnt < DATA_SIZE)))
            if truth0(((get0(tcb, "dev_in") !== nothing)))
                dev = get0(tcb, "dev_in")
                set0!(tcb, "dev_in", get0(dev, "link"))
                wa2 = get0(work, "a2")
                set0!(dev, "a1", get0(wa2, cnt))
                set0!(work, "a1", add0(cnt, 1))
                return scheduler_queue(sched, blocks, dev)
            end
        else
            set0!(tcb, "work_in", get0(work, "link"))
            return scheduler_queue(sched, blocks, work)
        end
    end
    return scheduler_suspend_current(sched)
end

function run_device(sched, blocks, tcb, packet)
    local v
    if truth0(((packet === nothing)))
        if truth0(((get0(tcb, "dev_in") === nothing)))
            return scheduler_suspend_current(sched)
        end
        v = get0(tcb, "dev_in")
        set0!(tcb, "dev_in", nothing)
        return scheduler_queue(sched, blocks, v)
    end
    set0!(tcb, "dev_in", packet)
    return scheduler_hold_current(sched)
end

function run_task(sched, blocks, tcb, packet)
    local fn_id
    fn_id = get0(tcb, "fn_id")
    if truth0(((fn_id == FN_IDLE)))
        return run_idle(sched, blocks, tcb, packet)
    end
    if truth0(((fn_id == FN_WORKER)))
        return run_worker(sched, blocks, tcb, packet)
    end
    if truth0(((fn_id == FN_HANDLER)))
        return run_handler(sched, blocks, tcb, packet)
    end
    return run_device(sched, blocks, tcb, packet)
end

function scheduler_schedule(sched, blocks)
    local packet, tcb
    set0!(sched, "current_tcb", get0(sched, "task_list"))
    while truth0(((get0(sched, "current_tcb") !== nothing)))
        tcb = get0(sched, "current_tcb")
        if truth0(tcb_is_held_or_suspended(tcb))
            set0!(sched, "current_tcb", get0(tcb, "link"))
        else
            set0!(sched, "current_id", get0(tcb, "id"))
            packet = nothing
            if truth0(((get0(tcb, "state") == STATE_SUSPENDED_RUNNABLE)))
                packet = get0(tcb, "queue")
                set0!(tcb, "queue", get0(packet, "link"))
                if truth0(((get0(tcb, "queue") === nothing)))
                    set0!(tcb, "state", STATE_RUNNING)
                else
                    set0!(tcb, "state", STATE_RUNNABLE)
                end
            end
            set0!(sched, "current_tcb", run_task(sched, blocks, tcb, packet))
        end
    end
    return nothing
end

function run_richards()
    local blocks, queue, sched
    sched = create_scheduler()
    blocks = mul0(Any[nothing], NUM_IDS)
    scheduler_add_idle_task(sched, blocks, ID_IDLE, 0, nothing, COUNT)
    queue = create_packet(nothing, ID_WORKER, KIND_WORK)
    queue = create_packet(queue, ID_WORKER, KIND_WORK)
    scheduler_add_worker_task(sched, blocks, ID_WORKER, 1000, queue)
    queue = create_packet(nothing, ID_DEVICE_A, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_A, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_A, KIND_DEVICE)
    scheduler_add_handler_task(sched, blocks, ID_HANDLER_A, 2000, queue)
    queue = create_packet(nothing, ID_DEVICE_B, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_B, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_B, KIND_DEVICE)
    scheduler_add_handler_task(sched, blocks, ID_HANDLER_B, 3000, queue)
    scheduler_add_device_task(sched, blocks, ID_DEVICE_A, 4000, nothing)
    scheduler_add_device_task(sched, blocks, ID_DEVICE_B, 5000, nothing)
    scheduler_schedule(sched, blocks)
    return (let _bool_value = ((get0(sched, "queue_count") == 2322)); truth0(_bool_value) ? ((get0(sched, "hold_count") == 928)) : _bool_value end)
end

