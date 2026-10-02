# port of test/benchmark/awfy/python/richards.py; algorithms retain their original control flow.
# The benchmark in its current state is a derivation from the SOM version,
# which is derived from Mario Wolczko's Smalltalk version of DeltaBlue.
#
# The original license details are availble here:
# http://web.archive.org/web/20050825101121/http://www.sunlabs.com/people/mario/java_benchmarking/index.html

# This file itself, and its souce control history is however based on the
# following. It is unclear whether this still bears any relevance since the
# nature of the code was essentially reverted back to the Smalltalk version.
#
# Derived from http://pws.prserv.net/dlissett/ben/bench1.htm
# Licensed CC BY-NC-SA 1.0
const _NO_TASK = nothing
const _NO_WORK = nothing
const _IDLER = 0
const _WORKER = 1
const _HANDLER_A = 2
const _HANDLER_B = 3
const _DEVICE_A = 4
const _DEVICE_B = 5
const _NUM_TYPES = 6
const _DEVICE_PACKET_KIND = 0
const _WORK_PACKET_KIND = 1
const _DATA_SIZE = 4
const _TRACING = false
abstract type A_Richards <: A_Benchmark end
mutable struct C_Richards <: A_Richards
    C_Richards(::Val{:raw}) = new()
end
function C_Richards(args...)
    self = C_Richards(Val(:raw))
    return self
end

function m_benchmark(self::A_Richards)
    return m_start(C__Scheduler())
end

function m_verify_result(self::A_Richards, result)
    return result
end

abstract type A__RBObject end
mutable struct C__RBObject <: A__RBObject
    C__RBObject(::Val{:raw}) = new()
end
function C__RBObject(args...)
    self = C__RBObject(Val(:raw))
    return self
end

function C__RBObject__append(packet, queue_head)
    local link, mouse
    packet.link = _NO_WORK
    if truth0(((_NO_WORK == queue_head)))
        return packet
    end
    mouse = queue_head
    while truth0(true)
        link = mouse.link
        if truth0(((link == _NO_WORK)))
            break
        end
        mouse = link
    end
    mouse.link = packet
    return queue_head
end

abstract type A__Scheduler <: A__RBObject end
mutable struct C__Scheduler <: A__Scheduler
    _layout::Int
    _task_list
    _current_task
    _current_task_identity::Int
    _task_table
    _queue_count::Int
    _hold_count::Int
    C__Scheduler(::Val{:raw}) = new(0, nothing, nothing, 0, nothing, 0, 0)
end
function C__Scheduler(args...)
    self = C__Scheduler(Val(:raw))
    init__Scheduler(self, args...)
    return self
end

function init__Scheduler(self)
    self._layout = 0
    self._task_list = _NO_TASK
    self._current_task = _NO_TASK
    self._current_task_identity = 0
    self._task_table = mul0(Any[_NO_TASK], _NUM_TYPES)
    self._queue_count = 0
    self._hold_count = 0
    return nothing
end

function m_create_device(self::A__Scheduler, identity, priority, work, state)
    local data
    data = C__DeviceTaskDataRecord()
    function fn(function_work, data_record)
        if truth0(((_NO_WORK == function_work)))
            function_work = data_record.pending
            if truth0(((_NO_WORK == function_work)))
                return m_mark_waiting(self)
            end
            data_record.pending = _NO_WORK
            return m_queue_packet(self, function_work)
        end
        data_record.pending = function_work
        if truth0(_TRACING)
            m__trace(self, function_work.datum)
        end
        return m__hold_self(self)
    end

    m_create_task(self, identity, priority, work, state, data, fn)
    return nothing
end

function m_create_handler(self::A__Scheduler, identity, priority, work, state)
    local data
    data = C__HandlerTaskDataRecord()
    function fn(work_arg, data_record)
        local count, device_packet, work_packet
        if truth0(((_NO_WORK != work_arg)))
            if truth0(((_WORK_PACKET_KIND == work_arg.kind)))
                m_work_in_add(data_record, work_arg)
            else
                m_device_in_add(data_record, work_arg)
            end
        end
        work_packet = data_record.work_in
        if truth0(((_NO_WORK == work_packet)))
            return m_mark_waiting(self)
        end
        count = work_packet.datum
        if truth0(((count >= _DATA_SIZE)))
            data_record.work_in = work_packet.link
            return m_queue_packet(self, work_packet)
        end
        device_packet = data_record.device_in
        if truth0(((_NO_WORK == device_packet)))
            return m_mark_waiting(self)
        end
        data_record.device_in = device_packet.link
        device_packet.datum = get0(work_packet.data, count)
        work_packet.datum = add0(count, 1)
        return m_queue_packet(self, device_packet)
    end

    m_create_task(self, identity, priority, work, state, data, fn)
    return nothing
end

function m_create_idler(self::A__Scheduler, identity, priority, work, state)
    local data
    data = C__IdleTaskDataRecord()
    function fn(_work, data_record)
        data_record.count = (data_record.count - 1)
        if truth0(((0 == data_record.count)))
            return m__hold_self(self)
        end
        if truth0(((0 == (data_record.control & 1))))
            data_record.control = fld(data_record.control, 2)
            return m_release(self, _DEVICE_A)
        end
        data_record.control = xor(fld(data_record.control, 2), 53256)
        return m_release(self, _DEVICE_B)
    end

    m_create_task(self, identity, priority, work, state, data, fn)
    return nothing
end

function m_create_packet(self::A__Scheduler, link, identity, kind)
    return C__Packet(link, identity, kind)
end

function m_create_task(self::A__Scheduler, identity, priority, work, state, data, fn)
    local t
    t = C__TaskControlBlock(self._task_list, identity, priority, work, state, data, fn)
    self._task_list = t
    set0!(self._task_table, identity, t)
    return nothing
end

function m_create_worker(self::A__Scheduler, identity, priority, work, state)
    local data
    data = C__WorkerTaskDataRecord()
    function fn(work, data_record)
        local i
        if truth0(((_NO_WORK == work)))
            return m_mark_waiting(self)
        end
        data_record.destination = (truth0(((_HANDLER_A == data_record.destination))) ? _HANDLER_B : _HANDLER_A)
        work.identity = data_record.destination
        work.datum = 0
        for i in range0(_DATA_SIZE)
            data_record.count = add0(data_record.count, 1)
            if truth0(((data_record.count > 26)))
                data_record.count = 1
            end
            set0!(work.data, i, (add0(65, data_record.count) - 1))
        end
        return m_queue_packet(self, work)
    end

    m_create_task(self, identity, priority, work, state, data, fn)
    return nothing
end

function m_start(self::A__Scheduler)
    local wkq
    m_create_idler(self, _IDLER, 0, _NO_WORK, C__TaskState__with_running())
    wkq = m_create_packet(self, _NO_WORK, _WORKER, _WORK_PACKET_KIND)
    wkq = m_create_packet(self, wkq, _WORKER, _WORK_PACKET_KIND)
    m_create_worker(self, _WORKER, 1000, wkq, C__TaskState__with_waiting_with_packet())
    wkq = m_create_packet(self, _NO_WORK, _DEVICE_A, _DEVICE_PACKET_KIND)
    wkq = m_create_packet(self, wkq, _DEVICE_A, _DEVICE_PACKET_KIND)
    wkq = m_create_packet(self, wkq, _DEVICE_A, _DEVICE_PACKET_KIND)
    m_create_handler(self, _HANDLER_A, 2000, wkq, C__TaskState__with_waiting_with_packet())
    wkq = m_create_packet(self, _NO_WORK, _DEVICE_B, _DEVICE_PACKET_KIND)
    wkq = m_create_packet(self, wkq, _DEVICE_B, _DEVICE_PACKET_KIND)
    wkq = m_create_packet(self, wkq, _DEVICE_B, _DEVICE_PACKET_KIND)
    m_create_handler(self, _HANDLER_B, 3000, wkq, C__TaskState__with_waiting_with_packet())
    m_create_device(self, _DEVICE_A, 4000, _NO_WORK, C__TaskState__with_waiting())
    m_create_device(self, _DEVICE_B, 5000, _NO_WORK, C__TaskState__with_waiting())
    m_schedule(self)
    return (let _bool_value = ((self._queue_count == 2322)); truth0(_bool_value) ? ((self._hold_count == 928)) : _bool_value end)
end

function m_find_task(self::A__Scheduler, identity)
    local t
    t = get0(self._task_table, identity)
    if truth0(((_NO_TASK == t)))
        throw(ErrorException("find_task failed"))
    end
    return t
end

function m__hold_self(self::A__Scheduler)
    self._hold_count = add0(self._hold_count, 1)
    m_set_task_holding(self._current_task, true)
    return self._current_task.link
end

function m_queue_packet(self::A__Scheduler, packet)
    local task
    task = m_find_task(self, packet.identity)
    if truth0(((_NO_TASK == task)))
        return _NO_TASK
    end
    self._queue_count = add0(self._queue_count, 1)
    packet.link = _NO_WORK
    packet.identity = self._current_task_identity
    return m_add_input_and_check_priority(task, packet, self._current_task)
end

function m_release(self::A__Scheduler, identity)
    local task
    task = m_find_task(self, identity)
    if truth0(((_NO_TASK == task)))
        return _NO_TASK
    end
    m_set_task_holding(task, false)
    if truth0(((task.priority > self._current_task.priority)))
        return task
    end
    return self._current_task
end

function m__trace(self::A__Scheduler, msg)
    self._layout = (self._layout - 1)
    if truth0(((0 >= self._layout)))
        println("")
        self._layout = 50
    end
    println(msg)
    return nothing
end

function m_mark_waiting(self::A__Scheduler)
    m_set_task_waiting(self._current_task, true)
    return self._current_task
end

function m_schedule(self::A__Scheduler)
    self._current_task = self._task_list
    while truth0(((_NO_TASK !== self._current_task)))
        if truth0(m_is_task_holding_or_waiting(self._current_task))
            self._current_task = self._current_task.link
        else
            self._current_task_identity = self._current_task.identity
            if truth0(_TRACING)
                m__trace(self, self._current_task_identity)
            end
            self._current_task = m_run_task(self._current_task)
        end
    end
    return nothing
end

abstract type A__DeviceTaskDataRecord <: A__RBObject end
mutable struct C__DeviceTaskDataRecord <: A__DeviceTaskDataRecord
    pending
    C__DeviceTaskDataRecord(::Val{:raw}) = new(nothing)
end
function C__DeviceTaskDataRecord(args...)
    self = C__DeviceTaskDataRecord(Val(:raw))
    init__DeviceTaskDataRecord(self, args...)
    return self
end

function init__DeviceTaskDataRecord(self)
    self.pending = _NO_WORK
    return nothing
end

abstract type A__HandlerTaskDataRecord <: A__RBObject end
mutable struct C__HandlerTaskDataRecord <: A__HandlerTaskDataRecord
    work_in
    device_in
    C__HandlerTaskDataRecord(::Val{:raw}) = new(nothing, nothing)
end
function C__HandlerTaskDataRecord(args...)
    self = C__HandlerTaskDataRecord(Val(:raw))
    init__HandlerTaskDataRecord(self, args...)
    return self
end

function init__HandlerTaskDataRecord(self)
    self.work_in = _NO_WORK
    self.device_in = _NO_WORK
    return nothing
end

function m_device_in_add(self::A__HandlerTaskDataRecord, packet)
    self.device_in = C__RBObject__append(packet, self.device_in)
    return nothing
end

function m_work_in_add(self::A__HandlerTaskDataRecord, packet)
    self.work_in = C__RBObject__append(packet, self.work_in)
    return nothing
end

abstract type A__IdleTaskDataRecord <: A__RBObject end
mutable struct C__IdleTaskDataRecord <: A__IdleTaskDataRecord
    control::Int
    count::Int
    C__IdleTaskDataRecord(::Val{:raw}) = new(0, 0)
end
function C__IdleTaskDataRecord(args...)
    self = C__IdleTaskDataRecord(Val(:raw))
    init__IdleTaskDataRecord(self, args...)
    return self
end

function init__IdleTaskDataRecord(self)
    self.control = 1
    self.count = 1000
    return nothing
end

abstract type A__Packet <: A__RBObject end
mutable struct C__Packet <: A__Packet
    link
    kind
    identity
    datum::Int
    data
    C__Packet(::Val{:raw}) = new(nothing, nothing, nothing, 0, nothing)
end
function C__Packet(args...)
    self = C__Packet(Val(:raw))
    init__Packet(self, args...)
    return self
end

function init__Packet(self, link, identity, kind)
    self.link = link
    self.kind = kind
    self.identity = identity
    self.datum = 0
    self.data = mul0(Any[0], _DATA_SIZE)
    return nothing
end

abstract type A__TaskState <: A__RBObject end
mutable struct C__TaskState <: A__TaskState
    _task_holding::Bool
    _task_waiting::Bool
    _packet_pending::Bool
    C__TaskState(::Val{:raw}) = new(false, false, false)
end
function C__TaskState(args...)
    self = C__TaskState(Val(:raw))
    init__TaskState(self, args...)
    return self
end

function init__TaskState(self)
    self._task_holding = false
    self._task_waiting = false
    self._packet_pending = false
    return nothing
end

function m_is_packet_pending(self::A__TaskState)
    return self._packet_pending
end

function m_is_task_waiting(self::A__TaskState)
    return self._task_waiting
end

function m_is_task_holding(self::A__TaskState)
    return self._task_holding
end

function m_set_task_holding(self::A__TaskState, task)
    self._task_holding = task
    return nothing
end

function m_set_task_waiting(self::A__TaskState, task)
    self._task_waiting = task
    return nothing
end

function m_packet_pending(self::A__TaskState)
    self._packet_pending = true
    self._task_waiting = false
    self._task_holding = false
    return self
end

function m_running(self::A__TaskState)
    let _assigned = false
        self._packet_pending = _assigned
        self._task_waiting = _assigned
        self._task_holding = _assigned
    end
    return self
end

function m_waiting(self::A__TaskState)
    let _assigned = false
        self._packet_pending = _assigned
        self._task_holding = _assigned
    end
    self._task_waiting = true
    return self
end

function m_waiting_with_packet(self::A__TaskState)
    self._task_holding = false
    let _assigned = true
        self._task_waiting = _assigned
        self._packet_pending = _assigned
    end
    return self
end

function m_is_task_holding_or_waiting(self::A__TaskState)
    return (let _bool_value = self._task_holding; truth0(_bool_value) ? _bool_value : (let _bool_value = !truth0(self._packet_pending); truth0(_bool_value) ? self._task_waiting : _bool_value end) end)
end

function m_is_waiting_with_packet(self::A__TaskState)
    return (let _bool_value = self._packet_pending; truth0(_bool_value) ? (let _bool_value = self._task_waiting; truth0(_bool_value) ? !truth0(self._task_holding) : _bool_value end) : _bool_value end)
end

function C__TaskState__with_running()
    return m_running(C__TaskState())
end

function C__TaskState__with_waiting()
    return m_waiting(C__TaskState())
end

function C__TaskState__with_waiting_with_packet()
    return m_waiting_with_packet(C__TaskState())
end

abstract type A__TaskControlBlock <: A__TaskState end
mutable struct C__TaskControlBlock <: A__TaskControlBlock
    _task_holding::Bool
    _task_waiting::Bool
    _packet_pending::Bool
    link
    identity
    task_function
    priority
    _input
    _handle
    C__TaskControlBlock(::Val{:raw}) = new(false, false, false, nothing, nothing, nothing, nothing, nothing, nothing)
end
function C__TaskControlBlock(args...)
    self = C__TaskControlBlock(Val(:raw))
    init__TaskControlBlock(self, args...)
    return self
end

function init__TaskControlBlock(self, link, identity, priority, initial_work_queue, initial_state, private_data, fn)
    init__TaskState(self)
    self.link = link
    self.identity = identity
    self.task_function = fn
    self.priority = priority
    self._input = initial_work_queue
    self._handle = private_data
    self._packet_pending = m_is_packet_pending(initial_state)
    self._task_waiting = m_is_task_waiting(initial_state)
    self._task_holding = m_is_task_holding(initial_state)
    return nothing
end

function m_add_input_and_check_priority(self::A__TaskControlBlock, packet, old_task)
    if truth0(((_NO_WORK == self._input)))
        self._input = packet
        self._packet_pending = true
        if truth0(((self.priority > old_task.priority)))
            return self
        end
    else
        self._input = C__RBObject__append(packet, self._input)
    end
    return old_task
end

function m_run_task(self::A__TaskControlBlock)
    local message
    if truth0(m_is_waiting_with_packet(self))
        message = self._input
        self._input = message.link
        if truth0(((_NO_WORK == self._input)))
            m_running(self)
        else
            m_packet_pending(self)
        end
    else
        message = _NO_WORK
    end
    return self.task_function(message, self._handle)
end

abstract type A__WorkerTaskDataRecord <: A__RBObject end
mutable struct C__WorkerTaskDataRecord <: A__WorkerTaskDataRecord
    destination
    count::Int
    C__WorkerTaskDataRecord(::Val{:raw}) = new(nothing, 0)
end
function C__WorkerTaskDataRecord(args...)
    self = C__WorkerTaskDataRecord(Val(:raw))
    init__WorkerTaskDataRecord(self, args...)
    return self
end

function init__WorkerTaskDataRecord(self)
    self.destination = _HANDLER_A
    self.count = 0
    return nothing
end

