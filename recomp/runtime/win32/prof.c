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
__attribute__((constructor)) static void prof_init(void)
{
    const char *path = getenv("W32_PROFILE"); if (!path || !*path) return;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0) return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Sample m = {0xffffffffu, 0, (uint64_t)(uintptr_t)&prof_init, 0}; ssize_t w = write(fd, &m, sizeof m); (void)w;
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, 0);
    struct itimerval it = {{0, 1000}, {0, 1000}}; setitimer(ITIMER_PROF, &it, 0);
#ifdef __APPLE__
    { pthread_t th; pthread_create(&th, 0, maps_thread, (void *)path); pthread_detach(th); }
#endif
}
