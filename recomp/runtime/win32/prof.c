/* W32_PROFILE=<file>: a sampling profiler for finding where the recompiled game spends its time (no perf needed).
 * SIGPROF fires every millisecond of process CPU time and lands in a running thread; the handler records that thread's
 * id and the interrupted instruction pointer, 16-byte records (u32 tid, u32 ms since start, u64 rip) appended to <file> with
 * write() (async-signal-safe) every 4096 samples. tools/profile.py maps the addresses to function names with nm. */
#define _GNU_SOURCE
#ifdef __APPLE__
#define _XOPEN_SOURCE 700                        /* the ucontext definitions */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#endif
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#ifndef __APPLE__
#include <sys/syscall.h>
#endif
#include <ucontext.h>
#include <time.h>

typedef struct { uint32_t tid, ms; uint64_t rip, lr; } Sample;   /* lr: the return address (arm64: who called a system function) */    /* ms: since start; the first record (tid ~0) holds prof_init's address */
static Sample buf[4096];
static _Atomic uint32_t nbuf;
static int fd = -1; static struct timespec t0;

static void on_prof(int sig, siginfo_t *si, void *uc_)
{
    (void)sig; (void)si;
    ucontext_t *uc = uc_;
    uint32_t k = nbuf++;
    if (k >= 4096) return;                       /* another thread is flushing: drop this sample */
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
#ifdef __APPLE__
    { uint64_t t = 0; pthread_threadid_np(NULL, &t); buf[k].tid = (uint32_t)t; }
#else
    buf[k].tid = (uint32_t)syscall(SYS_gettid);
#endif
    buf[k].ms = (uint32_t)((now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000);
#if defined(__x86_64__)
    buf[k].rip = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__APPLE__) && defined(__aarch64__)
    buf[k].rip = (uint64_t)uc->uc_mcontext->__ss.__pc; buf[k].lr = (uint64_t)uc->uc_mcontext->__ss.__lr;
#elif defined(__aarch64__)
    buf[k].rip = (uint64_t)uc->uc_mcontext.pc;
#endif
    if (k == 4095) { ssize_t w = write(fd, buf, sizeof buf); (void)w; nbuf = 0; }
}
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdio.h>
/* <file>.maps, rewritten every 2 s by a thread of its own (the image list isn't safe to read in the signal handler): each
 * loaded image's text range and name, so tools/profile.py can name the system library a sample landed in */
static void *maps_thread(void *arg)
{
    const char *path = arg; char tmp[1024]; snprintf(tmp, sizeof tmp, "%s.maps", path);
    for (;;) {
        FILE *f = fopen(tmp, "w");
        if (f) {
            for (uint32_t i = 0; i < _dyld_image_count(); i++) {
                const struct mach_header_64 *h = (const struct mach_header_64 *)_dyld_get_image_header(i); if (!h) continue;
                const struct load_command *lc = (const void *)(h + 1); intptr_t slide = _dyld_get_image_vmaddr_slide(i);
                for (uint32_t k = 0; k < h->ncmds; k++, lc = (const void *)((const char *)lc + lc->cmdsize))
                    if (lc->cmd == LC_SEGMENT_64 && !strcmp(((const struct segment_command_64 *)lc)->segname, "__TEXT")) {
                        const struct segment_command_64 *sg = (const void *)lc;
                        fprintf(f, "%llx %llx %s\n", (unsigned long long)(sg->vmaddr + slide), (unsigned long long)(sg->vmaddr + slide + sg->vmsize), _dyld_get_image_name(i));
                    }
            }
            fclose(f);
        }
        sleep(2);
    }
    return 0;
}
#endif
#ifdef __APPLE__
/* W32_PROFILE_GAME=1 (with W32_PROFILE): the game's main thread sampled by a thread of our own every millisecond of
 * wall time, whether it runs or waits. macOS hands SIGPROF to whichever thread it picks (often a sleeping one), so the
 * signal profile above can't say where the game's frame goes; here the thread is suspended, its registers read and its
 * frame chain walked, then resumed. Samples as above (tid 1 = the game's main thread); <file>.stacks holds each
 * sample's host frames (u32 count, u64 addresses) for tools/profile.py's inclusive view. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
_Atomic mach_port_t w32_prof_game_thread;          /* crash.c: set when the game's main thread registers */
static void *sampler(void *arg)
{
    char sp[1024]; snprintf(sp, sizeof sp, "%s.stacks", (const char *)arg);
    int sfd = open(sp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    static uint8_t sbuf[1 << 16]; size_t sn = 0;
    for (;;) {
        usleep(1000);
        mach_port_t th = w32_prof_game_thread; if (!th) continue;
        if (thread_suspend(th) != KERN_SUCCESS) continue;
        arm_thread_state64_t st; mach_msg_type_number_t n = ARM_THREAD_STATE64_COUNT;
        uint64_t fr[48]; uint32_t nf = 0;
        if (thread_get_state(th, ARM_THREAD_STATE64, (thread_state_t)&st, &n) == KERN_SUCCESS) {
            fr[nf++] = (uint64_t)arm_thread_state64_get_pc(st); fr[nf++] = (uint64_t)arm_thread_state64_get_lr(st);
            uint64_t fp = (uint64_t)arm_thread_state64_get_fp(st);
            for (int i = 0; i < 46 && fp && !(fp & 7); i++) {              /* [fp] caller's fp, [fp + 8] return address */
                uint64_t pair[2]; mach_vm_size_t got = 0;
                if (mach_vm_read_overwrite(mach_task_self(), fp, 16, (mach_vm_address_t)pair, &got) != KERN_SUCCESS || got != 16 || !pair[1]) break;
                if (i) fr[nf++] = pair[1];
                if (pair[0] <= fp) break; fp = pair[0];
            }
        }
        thread_resume(th);
        if (nf < 2) continue;
        uint32_t k = nbuf++; if (k >= 4096) { nbuf = 4096; continue; }
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        buf[k].tid = 1; buf[k].ms = (uint32_t)((now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000);
        buf[k].rip = fr[0]; buf[k].lr = fr[1];
        if (k == 4095) { ssize_t w = write(fd, buf, sizeof buf); (void)w; nbuf = 0; }
        if (sfd >= 0) {
            if (sn + 4 + 8 * nf > sizeof sbuf) { ssize_t w = write(sfd, sbuf, sn); (void)w; sn = 0; }
            memcpy(sbuf + sn, &nf, 4); memcpy(sbuf + sn + 4, fr, 8 * nf); sn += 4 + 8 * nf;
        }
    }
    return 0;
}
#endif
__attribute__((constructor)) static void prof_init(void)
{
    const char *path = getenv("W32_PROFILE"); if (!path || !*path) return;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0) return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Sample m = {0xffffffffu, 0, (uint64_t)(uintptr_t)&prof_init, 0}; ssize_t w = write(fd, &m, sizeof m); (void)w;
    fprintf(stderr, "prof: clock %.3f at the start\n", t0.tv_sec + t0.tv_nsec / 1e9);   /* tools/profile.py PROFILE_WINDOWS */
#ifdef __APPLE__
    if (getenv("W32_PROFILE_GAME")) {
        pthread_t th; pthread_create(&th, 0, sampler, (void *)path); pthread_detach(th);
        pthread_create(&th, 0, maps_thread, (void *)path); pthread_detach(th);
        return;
    }
#endif
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, 0);
    struct itimerval it = {{0, 1000}, {0, 1000}}; setitimer(ITIMER_PROF, &it, 0);
#ifdef __APPLE__
    { pthread_t th; pthread_create(&th, 0, maps_thread, (void *)path); pthread_detach(th); }
#endif
}
