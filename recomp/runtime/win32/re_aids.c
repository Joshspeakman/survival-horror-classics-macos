/* Test aids (the launch window's Options > Test Aids; SHC_AIDS=1): for checking every area's loading and effects
 * without dying on the way. Each frame the player's health is held at its maximum. All weapons: Resident Evil 2 by
 * GOG's own -wepall switch (the launcher passes it); the other games by their inventory (below, where known).
 *
 * The addresses are the GOG programs' own (found by watching memory as the player is hurt, then confirmed by holding
 * them: the player survives the opening attacks):
 *   Resident Evil 2   LeonU.exe 0x994646, ClaireU.exe 0x9946c6 (s16, the most is 200; read by the status screen's ECG)
 *   Resident Evil 3   0xa5ccc0 (s16), its maximum at 0xa5ccc2 (both programs: the game and The Mercenaries)
 *   Resident Evil     0xbe636c (s16; Chris 140, Jill 96: held at the most seen)
 *   Dino Crisis       0x6d8240 (s16), its maximum at 0x6d8242: Regina's actor is fixed in the game's state block
 *                     (0x6d66e0 + 0x1a48 = 0x6d8128; actors keep health at +0x118, the maximum at +0x11a). A new game
 *                     (0x40d580) sets it there from the difficulty's table at 0x644994 (1200 seen). */
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
#elif defined(RE1_EDITION)
#define RE_AID_HP 0x00be636cu                       /* Chris 140, Jill 96: held at the most seen this session
                                                       (Jill after Chris in one session is held at 140) */
#define RE_AID_HP_SEEN 1
#elif defined(DC_EDITION)
#define RE_AID_HP 0x006d8240u
#define RE_AID_HP_MAX 0x006d8242u
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
#elif defined(RE1_EDITION)                         /* USA/STAGEs/ROOMsrrp.RDT; stage and room are bytes */
#define RE_ROOM_HOOK 0x00477da2u
#define RE_STAGE 0x00be9820u
#define RE_ROOM 0x00be9821u
#define RE_ROOM_BYTES 1
#elif defined(RE3_EDITION)                         /* the room's name in the ROFS archives: Bio19/Data_j/rdt/RSRR.rdt */
#ifdef RE3_MERCE
#define RE_ROOM_HOOK 0x004685d8u
#else
#define RE_ROOM_HOOK 0x004684f8u
#endif
#define RE_STAGE 0x00a61fa6u
#define RE_ROOM 0x00a61fa8u
#define RE_CAMERA 0x00a61faau                      /* the camera (cut): a door sets it from its own data; a new game
                                                       leaves 0xb (its first room's), which other rooms may not have */
#ifdef RE3_MERCE                                    /* a door to another stage loads that stage's data first (0x42c3a2) */
#define RE_STAGE_LOAD 0x004689b0u
#else
#define RE_STAGE_LOAD 0x004688d0u
#endif
#elif defined(DC_EDITION)                          /* Data/stSRR.dat; the game's state block (0x6d66e0) holds the room at
                                                       +0x9ab0 and the stage at +0x9ab1 (stage 1 is 1). A new game writes
                                                       0x112 there (stage 1 room 12, 0x40d830); the hook is just after,
                                                       before the stage's and the room's files are read. Regina keeps
                                                       the new game's starting spot, which may be off another room's floor. */
#define RE_ROOM_HOOK 0x0040d839u
#define RE_STAGE 0x006e0191u
#define RE_ROOM 0x006e0190u
#define RE_ROOM_BYTES 1
#define RE_STAGE_FROM_1 1
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
#ifdef RE_ROOM_BYTES
            uint32_t cur_stage = G_MEM[RE_STAGE], cur_room = G_MEM[RE_ROOM];
#else
            uint32_t cur_stage = rt_r16(G_MEM, RE_STAGE), cur_room = rt_r16(G_MEM, RE_ROOM);
#endif
#ifndef RE_STAGE_FROM_1
#define RE_STAGE_FROM_1 0                               /* the games keep stage 1 as 0; Dino Crisis as 1 */
#endif
            cur_stage -= RE_STAGE_FROM_1;
            fprintf(stderr, "re: room jump: stage %u room %02x (was %u %02x)\n", stage, room, cur_stage + 1, cur_room);
            int other = cur_stage != stage - 1;
#ifdef RE_ROOM_BYTES
            G_MEM[RE_STAGE] = (uint8_t)(stage - 1 + RE_STAGE_FROM_1); G_MEM[RE_ROOM] = (uint8_t)room;
#else
            rt_w16(G_MEM, RE_STAGE, stage - 1 + RE_STAGE_FROM_1); rt_w16(G_MEM, RE_ROOM, room);
#endif
#ifdef RE_CAMERA
            rt_w16(G_MEM, RE_CAMERA, 0);                /* the room's first camera */
#endif
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
#elif defined(RE_AID_HP_SEEN)
    static int16_t most;
    if (hp > most && hp <= 2000) most = hp;   /* a new game starts at full health */
    if (most <= 0) return;
    want = most;
#else
    want = RE_AID_HP_FIXED;
#endif
    if (hp != want && hp > -1000) rt_w16(G_MEM, RE_AID_HP, (uint16_t)want);   /* hurt (or even past dying): healed at once */
#endif
}
#endif
