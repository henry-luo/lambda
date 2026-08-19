/* Native C2MIR port of awfy/richards.ls. */
/* OS kernel task scheduler simulation. Faithful port of the object graph:
 * packets/TCBs are heap-shaped records from static pools (reset per benchmark
 * run) and task polymorphism is kept as function-pointer dispatch, which is
 * the work the benchmark measures. Workload matches the .ls exactly:
 * 50 runs, 6 tasks, 8 packets, verified qpc=23246 hc=9297 per run. */
extern int printf(const char *, ...);

#define IDLER 0
#define WORKER 1
#define HANDLER_A 2
#define HANDLER_B 3
#define DEVICE_A 4
#define DEVICE_B 5
#define NUM_TYPES 6

#define DEVICE_PACKET_KIND 0
#define WORK_PACKET_KIND 1

#define DATA_SIZE 4

typedef struct Packet {
    struct Packet *link;
    int identity;
    int kind;
    int datum;
    int data[DATA_SIZE];
} Packet;

struct Scheduler;

typedef struct Tcb {
    struct Tcb *link;
    int identity;
    int priority;
    Packet *input;
    int pp; /* packet pending */
    int tw; /* task waiting */
    int th; /* task holding */
    void *handle;
    struct Tcb *(*fn)(Packet *work, void *handle, struct Scheduler *sched);
} Tcb;

typedef struct Scheduler {
    int qpc;
    int hc;
    Tcb *ct;
    int cti;
    Tcb *tl;
    Tcb *task_table[NUM_TYPES];
} Scheduler;

typedef struct DeviceData { Packet *pending; } DeviceData;
typedef struct HandlerData { Packet *work_in; Packet *device_in; } HandlerData;
typedef struct IdleData { int control; int icount; } IdleData;
typedef struct WorkerData { int destination; int wcount; } WorkerData;

/* Per-run object pools (reset at the start of each benchmark run). */
static Packet packet_pool[8];
static int packet_top;
static Tcb tcb_pool[NUM_TYPES];
static int tcb_top;

static Packet *create_packet(Packet *link, int identity, int kind) {
    Packet *pkt = &packet_pool[packet_top++];
    int i;
    pkt->link = link;
    pkt->identity = identity;
    pkt->kind = kind;
    pkt->datum = 0;
    for (i = 0; i < DATA_SIZE; i++) pkt->data[i] = 0;
    return pkt;
}

static Packet *append_packet(Packet *packet, Packet *queue_head) {
    Packet *mouse;
    packet->link = 0;
    if (queue_head == 0) return packet;
    mouse = queue_head;
    while (mouse->link != 0) mouse = mouse->link;
    mouse->link = packet;
    return queue_head;
}

static int tcb_is_held_or_waiting(Tcb *tcb) {
    if (tcb->th) return 1;
    if (!tcb->pp && tcb->tw) return 1;
    return 0;
}

static int tcb_is_waiting_with_packet(Tcb *tcb) {
    return tcb->pp && tcb->tw && !tcb->th;
}

static void tcb_set_running(Tcb *tcb) { tcb->pp = 0; tcb->tw = 0; tcb->th = 0; }
static void tcb_set_packet_pending(Tcb *tcb) { tcb->pp = 1; tcb->tw = 0; tcb->th = 0; }

static Tcb *tcb_add_input(Tcb *tcb, Packet *packet, Tcb *old_task) {
    if (tcb->input == 0) {
        tcb->input = packet;
        tcb->pp = 1;
        if (tcb->priority > old_task->priority) return tcb;
        return old_task;
    }
    tcb->input = append_packet(packet, tcb->input);
    return old_task;
}

static Tcb *tcb_run_task(Tcb *tcb, Scheduler *sched) {
    Packet *message = 0;
    if (tcb_is_waiting_with_packet(tcb)) {
        message = tcb->input;
        tcb->input = message->link;
        if (tcb->input == 0) tcb_set_running(tcb);
        else tcb_set_packet_pending(tcb);
    }
    return tcb->fn(message, tcb->handle, sched);
}

/* --- scheduler helpers --- */

static Tcb *find_task(Scheduler *sched, int identity) {
    return sched->task_table[identity];
}

static Tcb *hold_self(Scheduler *sched, Tcb *current_task) {
    sched->hc = sched->hc + 1;
    current_task->th = 1;
    return current_task->link;
}

static Tcb *mark_waiting(Tcb *current_task) {
    current_task->tw = 1;
    return current_task;
}

static Tcb *queue_packet(Scheduler *sched, Packet *packet, Tcb *current_task) {
    Tcb *t = find_task(sched, packet->identity);
    if (t == 0) return 0;
    sched->qpc = sched->qpc + 1;
    packet->link = 0;
    packet->identity = sched->cti;
    return tcb_add_input(t, packet, current_task);
}

static Tcb *release_task(Scheduler *sched, int identity, Tcb *current_task) {
    Tcb *t = find_task(sched, identity);
    if (t == 0) return 0;
    t->th = 0;
    if (t->priority > current_task->priority) return t;
    return current_task;
}

/* --- task functions --- */

static Tcb *task_fn_idle(Packet *work, void *handle, Scheduler *sched) {
    IdleData *data = (IdleData *) handle;
    Tcb *ct = sched->ct;
    data->icount = data->icount - 1;
    if (data->icount == 0) return hold_self(sched, ct);
    if ((data->control & 1) == 0) {
        data->control = data->control >> 1;
        return release_task(sched, DEVICE_A, ct);
    }
    data->control = (data->control >> 1) ^ 0xD008;
    return release_task(sched, DEVICE_B, ct);
}

static Tcb *task_fn_worker(Packet *work, void *handle, Scheduler *sched) {
    WorkerData *data = (WorkerData *) handle;
    Tcb *ct = sched->ct;
    int i;
    if (work == 0) return mark_waiting(ct);
    data->destination = (data->destination == HANDLER_A) ? HANDLER_B : HANDLER_A;
    work->identity = data->destination;
    work->datum = 0;
    for (i = 0; i < DATA_SIZE; i++) {
        data->wcount = data->wcount + 1;
        if (data->wcount > 26) data->wcount = 1;
        work->data[i] = 64 + data->wcount;
    }
    return queue_packet(sched, work, ct);
}

static Tcb *task_fn_handler(Packet *work, void *handle, Scheduler *sched) {
    HandlerData *data = (HandlerData *) handle;
    Tcb *ct = sched->ct;
    Packet *work_pkt;
    Packet *dev_pkt;
    int cnt;
    if (work != 0) {
        if (work->kind == WORK_PACKET_KIND) data->work_in = append_packet(work, data->work_in);
        else data->device_in = append_packet(work, data->device_in);
    }
    work_pkt = data->work_in;
    if (work_pkt == 0) return mark_waiting(ct);
    cnt = work_pkt->datum;
    if (cnt >= DATA_SIZE) {
        data->work_in = work_pkt->link;
        return queue_packet(sched, work_pkt, ct);
    }
    dev_pkt = data->device_in;
    if (dev_pkt == 0) return mark_waiting(ct);
    data->device_in = dev_pkt->link;
    dev_pkt->datum = work_pkt->data[cnt];
    work_pkt->datum = cnt + 1;
    return queue_packet(sched, dev_pkt, ct);
}

static Tcb *task_fn_device(Packet *work, void *handle, Scheduler *sched) {
    DeviceData *data = (DeviceData *) handle;
    Tcb *ct = sched->ct;
    if (work == 0) {
        Packet *fw = data->pending;
        if (fw == 0) return mark_waiting(ct);
        data->pending = 0;
        return queue_packet(sched, fw, ct);
    }
    data->pending = work;
    return hold_self(sched, ct);
}

/* --- scheduler --- */

static void create_task(Scheduler *sched, int identity, int priority, Packet *work,
                        int state_pp, int state_tw, int state_th, void *handle,
                        Tcb *(*fn)(Packet *, void *, Scheduler *)) {
    Tcb *tcb = &tcb_pool[tcb_top++];
    tcb->link = sched->tl;
    tcb->identity = identity;
    tcb->priority = priority;
    tcb->input = work;
    tcb->pp = state_pp;
    tcb->tw = state_tw;
    tcb->th = state_th;
    tcb->handle = handle;
    tcb->fn = fn;
    sched->tl = tcb;
    sched->task_table[identity] = tcb;
}

static void schedule(Scheduler *sched) {
    Tcb *ct = sched->tl;
    sched->ct = ct;
    while (ct != 0) {
        if (tcb_is_held_or_waiting(ct)) {
            ct = ct->link;
            sched->ct = ct;
        } else {
            sched->cti = ct->identity;
            sched->ct = ct;
            ct = tcb_run_task(ct, sched);
            sched->ct = ct;
        }
    }
}

static int benchmark(void) {
    Scheduler sched;
    Packet *workq;
    static IdleData idle_data;
    static WorkerData worker_data;
    static HandlerData handler_data_a;
    static HandlerData handler_data_b;
    static DeviceData device_data_a;
    static DeviceData device_data_b;
    int i;

    /* reset per-run pools and state records */
    packet_top = 0;
    tcb_top = 0;
    sched.qpc = 0;
    sched.hc = 0;
    sched.ct = 0;
    sched.cti = 0;
    sched.tl = 0;
    for (i = 0; i < NUM_TYPES; i++) sched.task_table[i] = 0;
    idle_data.control = 1;
    idle_data.icount = 10000;
    worker_data.destination = HANDLER_A;
    worker_data.wcount = 0;
    handler_data_a.work_in = 0;
    handler_data_a.device_in = 0;
    handler_data_b.work_in = 0;
    handler_data_b.device_in = 0;
    device_data_a.pending = 0;
    device_data_b.pending = 0;

    /* createIdler: running (pp=0, tw=0, th=0) */
    create_task(&sched, IDLER, 0, 0, 0, 0, 0, &idle_data, task_fn_idle);

    /* createWorker: waiting-with-packet (pp=1, tw=1, th=0) */
    workq = create_packet(0, WORKER, WORK_PACKET_KIND);
    workq = create_packet(workq, WORKER, WORK_PACKET_KIND);
    create_task(&sched, WORKER, 1000, workq, 1, 1, 0, &worker_data, task_fn_worker);

    /* createHandler A: waiting-with-packet */
    workq = create_packet(0, DEVICE_A, DEVICE_PACKET_KIND);
    workq = create_packet(workq, DEVICE_A, DEVICE_PACKET_KIND);
    workq = create_packet(workq, DEVICE_A, DEVICE_PACKET_KIND);
    create_task(&sched, HANDLER_A, 2000, workq, 1, 1, 0, &handler_data_a, task_fn_handler);

    /* createHandler B: waiting-with-packet */
    workq = create_packet(0, DEVICE_B, DEVICE_PACKET_KIND);
    workq = create_packet(workq, DEVICE_B, DEVICE_PACKET_KIND);
    workq = create_packet(workq, DEVICE_B, DEVICE_PACKET_KIND);
    create_task(&sched, HANDLER_B, 3000, workq, 1, 1, 0, &handler_data_b, task_fn_handler);

    /* createDevice: waiting (pp=0, tw=1, th=0) */
    create_task(&sched, DEVICE_A, 4000, 0, 0, 1, 0, &device_data_a, task_fn_device);
    create_task(&sched, DEVICE_B, 5000, 0, 0, 1, 0, &device_data_b, task_fn_device);

    schedule(&sched);

    if (sched.qpc == 23246 && sched.hc == 9297) return 1;
    printf("FAIL: qpc=%d hc=%d\n", sched.qpc, sched.hc);
    return 0;
}

int main(void) {
    /* 50 iterations: the canonical AWFY workload, matching richards.ls */
    int result = 0;
    int k;
    for (k = 0; k < 50; k++) result = benchmark();
    printf(result == 1 ? "Richards: PASS\n" : "Richards: FAIL\n");
    return result != 1;
}
