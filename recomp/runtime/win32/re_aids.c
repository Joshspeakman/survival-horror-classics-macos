/* Test aids (the launch window's Options > Test Aids; SHC_AIDS=1): for checking every area's loading and effects
 * without dying on the way. Each frame the player's health is held at its maximum. All weapons: Resident Evil 2 by
 * GOG's own -wepall switch (the launcher passes it); the other games by their inventory (below, where known).
 *
 * The addresses are the GOG programs' own (found by watching memory as the player is hurt, then confirmed by holding
 * them: the player survives the opening attacks):
 *   Resident Evil 2   LeonU.exe 0x994646, ClaireU.exe 0x9946c6 (s16, the most is 200; read by the status screen's ECG)
 *   Resident Evil 3   0xa5ccc0 (s16), its maximum at 0xa5ccc2 (both programs: the game and The Mercenaries)
 *   Resident Evil     0xbe63a6? / Dino Crisis: see RE_AID_HP below */
#include "w32.h"
#include "edition.h"
#ifdef RE_EDITION

#if defined(RE2_EDITION) && defined(RE2_CLAIRE)
#define RE_AID_HP 0x009946c6u
#define RE_AID_HP_FIXED 200
#elif defined(RE2_EDITION)
#define RE_AID_HP 0x00994646u
#define RE_AID_HP_FIXED 200
#elif defined(RE3_EDITION)
#define RE_AID_HP 0x00a5ccc0u
#define RE_AID_HP_MAX 0x00a5ccc2u
#endif

/* Room jump (SHC_ROOM=<stage><room>, as the room files are named: 101 = ROOM1010.RDT, stage 1 room 01): the first room
 * a new game loads is that one instead. Resident Evil 2: a hook just before the room loader reads the stage and room
 * (LeonU 0x4e1ecd, ClaireU 0x4e212d), which it turns into the file name (Pl0\\Rdu\\roomSRRP.rdt); Resident Evil 3 the same
 * before it names the room in its archives (0x4684f8; The Mercenaries 0x4685d8). */
#if defined(RE2_EDITION) && defined(RE2_CLAIRE)
#define RE_ROOM_HOOK 0x004e212du
#define RE_STAGE 0x00999194u
#define RE_ROOM 0x00999196u
#elif defined(RE2_EDITION)
#define RE_ROOM_HOOK 0x004e1ecdu
#define RE_STAGE 0x00999114u
#define RE_ROOM 0x00999116u
#elif defined(RE3_EDITION)                         /* the room's name in the ROFS archives: Bio19/Data_j/rdt/RSRR.rdt */
#ifdef RE3_MERCE
#define RE_ROOM_HOOK 0x004685d8u
#else
#define RE_ROOM_HOOK 0x004684f8u
#endif
#define RE_STAGE 0x00a61fa6u
#define RE_ROOM 0x00a61fa8u
#ifdef RE3_MERCE                                    /* a door to another stage loads that stage's data first (0x42c3a2) */
#define RE_STAGE_LOAD 0x004689b0u
#else
#define RE_STAGE_LOAD 0x004688d0u
#endif
#endif
int re_hook(Ctx *c, uint32_t addr)
{
#ifdef RE_ROOM_HOOK
    static int loads, at = -1;                      /* SHC_ROOM_AT=<n>: at the n-th room load (default 1) */
    if (addr == RE_ROOM_HOOK && at < 0) { const char *a = getenv("SHC_ROOM_AT"); at = a && atoi(a) > 0 ? atoi(a) : 1; }
    if (addr == RE_ROOM_HOOK && ++loads == at) {
        const char *e = getenv("SHC_ROOM");
        unsigned stage = 0, room = 0;
        if (e && strlen(e) >= 3 && sscanf(e, "%1x%2x", &stage, &room) == 2 && stage >= 1) {
            fprintf(stderr, "re: room jump: stage %u room %02x (was %u %02x)\n", stage, room, rt_r16(G_MEM, RE_STAGE) + 1, rt_r16(G_MEM, RE_ROOM));
            int other = rt_r16(G_MEM, RE_STAGE) != stage - 1;
            rt_w16(G_MEM, RE_STAGE, stage - 1); rt_w16(G_MEM, RE_ROOM, room);
#ifdef RE_STAGE_LOAD
            if (other) {                                /* as a door to another stage does: its data, then the room */
                Ctx save = *c; c->esp -= 4; rt_w32(G_MEM, c->esp, 0xffeff000u); rt_call(c, RE_STAGE_LOAD);
                uint32_t keep = c->esp; *c = save; (void)keep;
            }
#endif
            (void)other;
        }
        return 0;
    }
#endif
    (void)addr; return 0;
}
static int aids_on(void)
{
    static int on = -1; if (on < 0) { const char *e = getenv("SHC_AIDS"); on = e && atoi(e) > 0; if (on) fprintf(stderr, "re: test aids on\n"); }
    return on;
}
void re_aids_tick(void)                        /* the host calls it once a frame */
{
    if (!aids_on()) return;
#ifdef RE_AID_HP
    int16_t hp = (int16_t)rt_r16(G_MEM, RE_AID_HP), want;
#ifdef RE_AID_HP_MAX
    want = (int16_t)rt_r16(G_MEM, RE_AID_HP_MAX);
    if (want <= 0 || want > 2000) return;      /* not in a game */
#else
    want = RE_AID_HP_FIXED;
#endif
    if (hp != want && hp > -1000) rt_w16(G_MEM, RE_AID_HP, (uint16_t)want);   /* hurt (or even past dying): healed at once */
#endif
}
#endif
