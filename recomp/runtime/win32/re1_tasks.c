/* Resident Evil's cooperative tasks. The game (like its PlayStation original) runs its work as a few tasks that switch by
 * hand: starting one saves the scheduler's stack pointer and loads the task's, then jumps to its entry; a task yields by
 * saving its own and loading the scheduler's, and is resumed the other way round; a task that ends goes back to the
 * scheduler's stack. In recompiled code the host's call stack cannot be swapped like that, so each task runs on a host
 * thread of its own, with its own guest registers, and these four routines hand over between them: exactly one of the
 * scheduler and the tasks runs at a time, as in the game.
 *   0x47575c start(task [0xd91a7c]): entry [0xd91a80 + 4k], stack [0xd91a70 + 4k]
 *   0x47577c yield (on a task)      0x47579c resume(task [0xd91a7c])      0x4757a8 end (on a task)
 * The guest's own records (each task's stack pointer, the scheduler's at 0xd91a8c) are kept up to date as the game
 * would leave them, in case it reads them. */
#include "w32.h"
#include "edition.h"
#ifdef RE1_EDITION
#include <pthread.h>
#include <setjmp.h>

#define T_INDEX 0xd91a7cu
#define T_STACK 0xd91a70u
#define T_ENTRY 0xd91a80u
#define T_MAIN_ESP 0xd91a8cu
#define NT 8
typedef struct Task { int live, die; pthread_t pt; Ctx c; uint32_t entry; jmp_buf base; } Task;
static Task tasks[NT];
static pthread_mutex_t tm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t tcv = PTHREAD_COND_INITIALIZER;
static int turn = -1;                                    /* who runs: -1 the scheduler, else the task */
static __thread int my_task = -1;
extern __thread Ctx *w32_cur_ctx;
static int tlog(void) { static int on = -1; if (on < 0) on = getenv("RE1_TASKLOG") != 0; return on; }

static void wait_turn(int who)                           /* tm held */
{
    while (turn != who && !(who >= 0 && tasks[who].die)) pthread_cond_wait(&tcv, &tm);
}
static void hand_to(int who) { turn = who; pthread_cond_broadcast(&tcv); }
static void *task_main(void *arg)
{
    Task *t = arg; int k = (int)(t - tasks); my_task = k; w32_cur_ctx = &t->c;
    pthread_mutex_lock(&tm); wait_turn(k); int die = t->die; pthread_mutex_unlock(&tm);
    if (!die && !setjmp(t->base)) rt_call(&t->c, t->entry);   /* returns only if the entry returns (it ends by the task end) */
    pthread_mutex_lock(&tm);
    if (tlog()) fprintf(stderr, "re1: task %d %s\n", k, t->die ? "dropped" : "ended");
    t->live = 0; if (turn == k) hand_to(-1); pthread_cond_broadcast(&tcv);
    pthread_mutex_unlock(&tm);
    return 0;
}
static void run_task(int k)                              /* the scheduler: task k runs until it yields or ends; tm held */
{
    rt_w32(G_MEM, T_MAIN_ESP, w32_cur_ctx ? w32_cur_ctx->esp : 0);
    hand_to(k); wait_turn(-1);
}
int re1_task_override(Ctx *c, uint32_t addr)
{
    uint32_t k = rt_r32(G_MEM, T_INDEX);
    switch (addr) {
    case 0x0047575cu: {                                  /* start task k at its entry, on its stack */
        if (k >= NT) return 0;
        pthread_mutex_lock(&tm);
        Task *t = &tasks[k];
        if (t->live) {                                   /* a task left parked in this slot: drop it first */
            t->die = 1; pthread_cond_broadcast(&tcv);
            while (t->live) pthread_cond_wait(&tcv, &tm);
            pthread_join(t->pt, 0);
        } else if (t->pt) { pthread_join(t->pt, 0); }
        memset(t, 0, sizeof *t);
        t->c = *c; t->c.esp = rt_r32(G_MEM, T_STACK + 4 * k); t->entry = rt_r32(G_MEM, T_ENTRY + 4 * k);
        t->c.esp -= 4; rt_w32(G_MEM, t->c.esp, 0xffeff000u);      /* the entry returns here (host return) */
        t->live = 1;
        if (tlog()) fprintf(stderr, "re1: task %u starts at %08x (stack %08x)\n", k, t->entry, t->c.esp);
        pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 64u << 20);
        pthread_create(&t->pt, &a, task_main, t); pthread_attr_destroy(&a);
        run_task((int)k);
        pthread_mutex_unlock(&tm);
        c->esp += 4; return 1;                           /* (its ret) */
    }
    case 0x0047579cu: {                                  /* resume task k */
        if (k >= NT) return 0;
        pthread_mutex_lock(&tm);
        if (tasks[k].live) run_task((int)k);
        else if (tlog()) fprintf(stderr, "re1: resume of task %u, which is not running\n", k);
        pthread_mutex_unlock(&tm);
        c->esp += 4; return 1;
    }
    case 0x0047577cu: {                                  /* the running task yields to the scheduler */
        int me = my_task; if (me < 0) return 0;
        pthread_mutex_lock(&tm);
        rt_w32(G_MEM, T_STACK + 4 * (uint32_t)me, c->esp);
        hand_to(-1); wait_turn(me); int die = tasks[me].die;
        pthread_mutex_unlock(&tm);
        if (die) longjmp(tasks[me].base, 1);
        c->esp += 4; return 1;
    }
    case 0x004757a8u: {                                  /* the running task ends */
        int me = my_task; if (me < 0) return 0;
        longjmp(tasks[me].base, 1);
    }
    }
    return 0;
}
#endif
