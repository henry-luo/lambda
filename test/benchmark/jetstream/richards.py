#!/usr/bin/env python3
"""JetStream Benchmark: richards (Octane) — Python version
OS kernel task scheduler simulation
Original: Martin Richards (BCPL), V8 project authors
Simulates task dispatching with idle, worker, handler, and device tasks
"""
import time

ID_IDLE      = 0
ID_WORKER    = 1
ID_HANDLER_A = 2
ID_HANDLER_B = 3
ID_DEVICE_A  = 4
ID_DEVICE_B  = 5
NUM_IDS      = 6

KIND_DEVICE = 0
KIND_WORK   = 1
DATA_SIZE   = 4
COUNT       = 1000

STATE_RUNNING            = 0
STATE_RUNNABLE           = 1
STATE_SUSPENDED          = 2
STATE_HELD               = 4
STATE_SUSPENDED_RUNNABLE = 3

FN_IDLE    = 0
FN_WORKER  = 1
FN_HANDLER = 2
FN_DEVICE  = 3


def create_packet(link, pid, kind):
    return {'link': link, 'id': pid, 'kind': kind, 'a1': 0, 'a2': [0] * DATA_SIZE}


def packet_add_to(packet, queue):
    packet['link'] = None
    if queue is None:
        return packet
    nxt = queue
    peek = nxt['link']
    while peek is not None:
        nxt = peek
        peek = nxt['link']
    nxt['link'] = packet
    return queue


def create_tcb(link, tid, priority, queue, state, fn_id):
    return {'link': link, 'id': tid, 'priority': priority, 'queue': queue,
            'state': state, 'fn_id': fn_id,
            'v1': 0, 'v2': 0, 'work_in': None, 'dev_in': None}


def tcb_is_held_or_suspended(tcb):
    return bool(tcb['state'] & STATE_HELD) or tcb['state'] == STATE_SUSPENDED


def create_scheduler():
    return {'queue_count': 0, 'hold_count': 0,
            'task_list': None, 'current_tcb': None, 'current_id': 0}


def scheduler_add_task(sched, blocks, tid, pri, queue, state, fn_id):
    tcb = create_tcb(sched['task_list'], tid, pri, queue, state, fn_id)
    sched['task_list'] = tcb
    blocks[tid] = tcb
    sched['current_tcb'] = tcb


def scheduler_add_idle_task(sched, blocks, tid, pri, queue, count):
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_RUNNABLE, FN_IDLE)
    tcb = sched['current_tcb']
    tcb['v1'] = 1
    tcb['v2'] = count


def scheduler_add_worker_task(sched, blocks, tid, pri, queue):
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_SUSPENDED_RUNNABLE, FN_WORKER)
    tcb = sched['current_tcb']
    tcb['v1'] = ID_HANDLER_A
    tcb['v2'] = 0


def scheduler_add_handler_task(sched, blocks, tid, pri, queue):
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_SUSPENDED_RUNNABLE, FN_HANDLER)


def scheduler_add_device_task(sched, blocks, tid, pri, queue):
    scheduler_add_task(sched, blocks, tid, pri, queue, STATE_SUSPENDED, FN_DEVICE)


def scheduler_release(sched, blocks, tid):
    tcb = blocks[tid]
    if tcb is None:
        return tcb
    tcb['state'] &= ~STATE_HELD
    cur = sched['current_tcb']
    if tcb['priority'] > cur['priority']:
        return tcb
    return cur


def scheduler_hold_current(sched):
    sched['hold_count'] += 1
    tcb = sched['current_tcb']
    tcb['state'] |= STATE_HELD
    return tcb['link']


def scheduler_suspend_current(sched):
    tcb = sched['current_tcb']
    tcb['state'] |= STATE_SUSPENDED
    return tcb


def scheduler_queue(sched, blocks, packet):
    t = blocks[packet['id']]
    if t is None:
        return t
    sched['queue_count'] += 1
    packet['link'] = None
    packet['id'] = sched['current_id']
    cur = sched['current_tcb']
    if t['queue'] is None:
        t['queue'] = packet
        t['state'] |= STATE_RUNNABLE
        if t['priority'] > cur['priority']:
            return t
    else:
        t['queue'] = packet_add_to(packet, t['queue'])
    return cur


def run_idle(sched, blocks, tcb, packet):
    tcb['v2'] -= 1
    if tcb['v2'] == 0:
        return scheduler_hold_current(sched)
    if (tcb['v1'] & 1) == 0:
        tcb['v1'] >>= 1
        return scheduler_release(sched, blocks, ID_DEVICE_A)
    else:
        tcb['v1'] = (tcb['v1'] >> 1) ^ 53256
        return scheduler_release(sched, blocks, ID_DEVICE_B)


def run_worker(sched, blocks, tcb, packet):
    if packet is None:
        return scheduler_suspend_current(sched)
    if tcb['v1'] == ID_HANDLER_A:
        tcb['v1'] = ID_HANDLER_B
    else:
        tcb['v1'] = ID_HANDLER_A
    packet['id'] = tcb['v1']
    packet['a1'] = 0
    pkt_a2 = packet['a2']
    for i in range(DATA_SIZE):
        tcb['v2'] += 1
        if tcb['v2'] > 26:
            tcb['v2'] = 1
        pkt_a2[i] = tcb['v2']
    return scheduler_queue(sched, blocks, packet)


def run_handler(sched, blocks, tcb, packet):
    if packet is not None:
        if packet['kind'] == KIND_WORK:
            tcb['work_in'] = packet_add_to(packet, tcb['work_in'])
        else:
            tcb['dev_in'] = packet_add_to(packet, tcb['dev_in'])
    if tcb['work_in'] is not None:
        work = tcb['work_in']
        cnt = work['a1']
        if cnt < DATA_SIZE:
            if tcb['dev_in'] is not None:
                dev = tcb['dev_in']
                tcb['dev_in'] = dev['link']
                wa2 = work['a2']
                dev['a1'] = wa2[cnt]
                work['a1'] = cnt + 1
                return scheduler_queue(sched, blocks, dev)
        else:
            tcb['work_in'] = work['link']
            return scheduler_queue(sched, blocks, work)
    return scheduler_suspend_current(sched)


def run_device(sched, blocks, tcb, packet):
    if packet is None:
        if tcb['dev_in'] is None:
            return scheduler_suspend_current(sched)
        v = tcb['dev_in']
        tcb['dev_in'] = None
        return scheduler_queue(sched, blocks, v)
    tcb['dev_in'] = packet
    return scheduler_hold_current(sched)


def run_task(sched, blocks, tcb, packet):
    fn_id = tcb['fn_id']
    if fn_id == FN_IDLE:
        return run_idle(sched, blocks, tcb, packet)
    if fn_id == FN_WORKER:
        return run_worker(sched, blocks, tcb, packet)
    if fn_id == FN_HANDLER:
        return run_handler(sched, blocks, tcb, packet)
    return run_device(sched, blocks, tcb, packet)


def scheduler_schedule(sched, blocks):
    sched['current_tcb'] = sched['task_list']
    while sched['current_tcb'] is not None:
        tcb = sched['current_tcb']
        if tcb_is_held_or_suspended(tcb):
            sched['current_tcb'] = tcb['link']
        else:
            sched['current_id'] = tcb['id']
            packet = None
            if tcb['state'] == STATE_SUSPENDED_RUNNABLE:
                packet = tcb['queue']
                tcb['queue'] = packet['link']
                if tcb['queue'] is None:
                    tcb['state'] = STATE_RUNNING
                else:
                    tcb['state'] = STATE_RUNNABLE
            sched['current_tcb'] = run_task(sched, blocks, tcb, packet)


def run_richards():
    sched = create_scheduler()
    blocks = [None] * NUM_IDS

    scheduler_add_idle_task(sched, blocks, ID_IDLE, 0, None, COUNT)

    queue = create_packet(None, ID_WORKER, KIND_WORK)
    queue = create_packet(queue, ID_WORKER, KIND_WORK)
    scheduler_add_worker_task(sched, blocks, ID_WORKER, 1000, queue)

    queue = create_packet(None, ID_DEVICE_A, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_A, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_A, KIND_DEVICE)
    scheduler_add_handler_task(sched, blocks, ID_HANDLER_A, 2000, queue)

    queue = create_packet(None, ID_DEVICE_B, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_B, KIND_DEVICE)
    queue = create_packet(queue, ID_DEVICE_B, KIND_DEVICE)
    scheduler_add_handler_task(sched, blocks, ID_HANDLER_B, 3000, queue)

    scheduler_add_device_task(sched, blocks, ID_DEVICE_A, 4000, None)
    scheduler_add_device_task(sched, blocks, ID_DEVICE_B, 5000, None)

    scheduler_schedule(sched, blocks)

    return sched['queue_count'] == 2322 and sched['hold_count'] == 928


def main():
    t0 = time.perf_counter_ns()
    pass_all = True
    for _ in range(50):
        if not run_richards():
            pass_all = False
    t1 = time.perf_counter_ns()

    if pass_all:
        print("richards: PASS")
    else:
        print("richards: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()
