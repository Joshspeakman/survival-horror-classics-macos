/* Dino Crisis 2's cooperative tasks (as its PlayStation original): four task slots at 0x73c870, 0x20 bytes each
 * (+0 state: 0 free, 1 asleep for +2 ticks, 2 ready; +4 the task's saved stack pointer), the running slot's offset at
 * 0x73c8f0, the scheduler's stack pointer at 0x73c8f4 and the base of the tasks' stacks (0x20000 apart) at 0x73c8f8.
 *   0x4c2900 schedule(): for each slot, a sleeping one counts down; a ready one is switched to (its saved registers
 *            popped off its stack, then ret) and runs until it sleeps or restarts, which jump back to 0x4c2950
 *   0x4c2974 create(slot, entry)   0x4c29a9 sleep(ticks) on a task   0x4c29d6 restart(entry) on a task
 *   0x4c2a0d kill(slot)
 * Recompiled code can't swap the host's call stack, so (as for Resident Evil's tasks, re1_tasks.c) each task runs on a
 * host thread of its own and these routines hand over between them and the scheduler: exactly one runs at a time, as
 * in the game. The guest's records (states, counts, saved stack pointers) are kept as the game would leave them. */
#include "w32.h"
#include "edition.h"
#ifdef DC2_EDITION
#include <pthread.h>
#include <setjmp.h>

#define T_TABLE 0x0073c870u
#define T_CUR 0x0073c8f0u
#define T_MAIN_ESP 0x0073c8f4u
#define T_BASE 0x0073c8f8u
#define NT 4
typedef struct Task { int live, die, fresh; pthread_t pt; Ctx c; uint32_t entry; jmp_buf base; } Task;
static Task tasks[NT];
static pthread_mutex_t tm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t tcv = PTHREAD_COND_INITIALIZER;
static int turn = -1;                                    /* who runs: -1 the scheduler, else the task */
static __thread int my_task = -1;
extern __thread Ctx *w32_cur_ctx;
static int tlog(void) { static int on = -1; if (on < 0) on = getenv("DC2_TASKLOG") != 0; return on; }
static uint16_t st_of(int k) { return rt_r16(G_MEM, T_TABLE + 0x20u * (uint32_t)k); }

static void wait_turn(int who) { while (turn != who && !(who >= 0 && tasks[who].die)) pthread_cond_wait(&tcv, &tm); }   /* tm held */
static void hand_to(int who) { turn = who; pthread_cond_broadcast(&tcv); }
static void *task_main(void *arg)
{
    Task *t = arg; int k = (int)(t - tasks); my_task = k; w32_cur_ctx = &t->c;
    pthread_mutex_lock(&tm); wait_turn(k); int die = t->die; pthread_mutex_unlock(&tm);
    if (!die && !setjmp(t->base)) rt_call(&t->c, t->entry);   /* a task's entry never returns (it sleeps or restarts) */
    pthread_mutex_lock(&tm);
    if (tlog()) fprintf(stderr, "dc2: task %d %s\n", k, t->die ? "dropped" : "left");
    t->live = 0; if (turn == k) hand_to(-1); pthread_cond_broadcast(&tcv);
    pthread_mutex_unlock(&tm);
    return 0;
}
static void drop(int k)                                  /* a task's host thread, if any, ends (tm held) */
{
    Task *t = &tasks[k];
    if (t->live) { t->die = 1; pthread_cond_broadcast(&tcv); while (t->live) pthread_cond_wait(&tcv, &tm); }
    if (t->pt) { pthread_join(t->pt, 0); t->pt = 0; }
    t->die = 0;
}
/* a new host thread for slot k from its fresh stack: the frame the game made (6 registers, then the entry) */
static void start(Ctx *c, int k)                         /* tm held */
{
    Task *t = &tasks[k]; drop(k);
    uint32_t sp = rt_r32(G_MEM, T_TABLE + 0x20u * (uint32_t)k + 4);
    t->c = *c; t->c.ebp = rt_r32(G_MEM, sp); t->c.edi = rt_r32(G_MEM, sp + 4); t->c.esi = rt_r32(G_MEM, sp + 8);
    t->c.edx = rt_r32(G_MEM, sp + 12); t->c.ecx = rt_r32(G_MEM, sp + 16); t->c.ebx = rt_r32(G_MEM, sp + 20);
    t->entry = rt_r32(G_MEM, sp + 24); t->c.esp = sp + 28 - 4; rt_w32(G_MEM, t->c.esp, 0xffeff000u);   /* (host return) */
    t->live = 1; t->fresh = 0;
    if (tlog()) fprintf(stderr, "dc2: task %d starts at %08x (stack %08x)\n", k, t->entry, t->c.esp);
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 64u << 20);
    pthread_create(&t->pt, &a, task_main, t); pthread_attr_destroy(&a);
}
int dc2_task_override(Ctx *c, uint32_t addr)
{
    switch (addr) {
    case 0x004c2900u: {                                  /* the scheduler: one round over the slots */
        pthread_mutex_lock(&tm);
        rt_w32(G_MEM, T_MAIN_ESP, c->esp);
        for (int k = 0; k < NT; k++) {
            uint32_t s = T_TABLE + 0x20u * (uint32_t)k;
            rt_w32(G_MEM, T_CUR, 0x20u * (uint32_t)k);
            if (st_of(k) == 1) { uint16_t n = (uint16_t)(rt_r16(G_MEM, s + 2) - 1); rt_w16(G_MEM, s + 2, n); if (!n) rt_w16(G_MEM, s, 2); }
            if (st_of(k) != 2) continue;
            if (tasks[k].fresh || !tasks[k].live) start(c, k);
            hand_to(k); wait_turn(-1);
        }
        pthread_mutex_unlock(&tm);
        c->esp += 4; return 1;
    }
    case 0x004c2974u: {                                  /* create(slot, entry): its stack and first frame, as the game makes them */
        uint32_t k = rt_r32(G_MEM, c->esp + 4), entry = rt_r32(G_MEM, c->esp + 8); if (k >= NT) return 0;
        uint32_t sp = rt_r32(G_MEM, T_BASE) - k * 0x20000u - 0x1c;
        rt_w16(G_MEM, T_TABLE + 0x20u * k, 2); rt_w32(G_MEM, T_TABLE + 0x20u * k + 4, sp); rt_w32(G_MEM, sp + 0x18, entry);
        pthread_mutex_lock(&tm); tasks[k].fresh = 1; pthread_mutex_unlock(&tm);
        if (tlog()) fprintf(stderr, "dc2: create task %u at %08x\n", k, entry);
        c->eax = sp; c->esp += 4; return 1;
    }
    case 0x004c29a9u: {                                  /* sleep(ticks) on the running task: back to the scheduler */
        int me = my_task; if (me < 0) return 0;
        uint32_t s = T_TABLE + 0x20u * (uint32_t)me;
        rt_w16(G_MEM, s, 1); rt_w16(G_MEM, s + 2, (uint16_t)rt_r32(G_MEM, c->esp + 4));
        rt_w32(G_MEM, s + 4, c->esp - 24);                 /* (where the game's own frame would be) */
        pthread_mutex_lock(&tm);
        hand_to(-1); wait_turn(me); int die = tasks[me].die;
        pthread_mutex_unlock(&tm);
        if (die) longjmp(tasks[me].base, 1);
        c->esp += 4; return 1;
    }
    case 0x004c29d6u: {                                  /* restart(entry) on the running task: a fresh stack at the entry */
        int me = my_task; if (me < 0) return 0;
        uint32_t entry = rt_r32(G_MEM, c->esp + 4), s = T_TABLE + 0x20u * (uint32_t)me;
        uint32_t sp = rt_r32(G_MEM, T_BASE) - (uint32_t)me * 0x4000u - 0x1c;   /* (0x4000: as the game computes it) */
        rt_w16(G_MEM, s, 2); rt_w32(G_MEM, s + 4, sp); rt_w32(G_MEM, sp + 0x18, entry);
        if (tlog()) fprintf(stderr, "dc2: task %d restarts at %08x\n", me, entry);
        pthread_mutex_lock(&tm); tasks[me].fresh = 1; hand_to(-1); pthread_mutex_unlock(&tm);
        longjmp(tasks[me].base, 1);                     /* this thread ends; the scheduler starts the slot again next round */
    }
    case 0x004c2a0du: {                                  /* kill(slot) */
        uint32_t k = rt_r32(G_MEM, c->esp + 4); if (k >= NT) return 0;
        for (uint32_t o = 0; o < 0x18; o += 4) rt_w32(G_MEM, T_TABLE + 0x20u * k + o, 0);
        if ((int)k != my_task) { pthread_mutex_lock(&tm); if (tasks[k].live) { tasks[k].die = 1; pthread_cond_broadcast(&tcv); } tasks[k].fresh = 0; pthread_mutex_unlock(&tm); }
        if (tlog()) fprintf(stderr, "dc2: kill task %u\n", k);
        c->esp += 4; return 1;
    }
    }
    return 0;
}
#endif
