/* Address-specific hooks and user-data names must follow the compiled executable. */
#ifndef DS2_EDITION_H
#define DS2_EDITION_H
#if defined(GTA3_EDITION) || defined(GTAVC_EDITION) || defined(GTASA_EDITION)
#define GTA_EDITION 1   /* what the three Grand Theft Auto games share (launcher, window, video mode) */
#endif
#ifdef GTA_EDITION
#if defined(GTA3_EDITION)
/* Grand Theft Auto III (the Mac Steam release, app 12230: gta3.exe 1.1 from TransGaming's Cider wrapper, unwrapped by
 * tools/steamstub21.py): RenderWare on Direct3D 8; no Dungeon Siege hooks */
#define DS2_IMAGE_END 0x009ea000u
#define DS2_TITLE "Grand Theft Auto III"
#define DS2_DOCUMENTS "GTA3 User Files"
#define DS2_INI "gta3.ini"
#define GAME_EXE "gta3.exe"
#elif defined(GTAVC_EDITION)
/* Grand Theft Auto: Vice City (the Windows Steam release, app 12110 depot 12111: gta-vc.exe, unwrapped by
 * tools/steamstub21.py; the Mac release's copy has SecuROM with online activation): RenderWare on Direct3D 8 */
#define DS2_IMAGE_END 0x00a96000u
#define DS2_TITLE "Grand Theft Auto: Vice City"
#define DS2_DOCUMENTS "GTA Vice City User Files"
#define DS2_INI "gta-vc.ini"
#define GAME_EXE "gta-vc.exe"
#else
/* Grand Theft Auto: San Andreas (the Mac Steam release, app 12250: gta_sa.exe 3.0 from the Cider wrapper, unwrapped
 * by tools/steamstub21.py): RenderWare on Direct3D 9, DirectSound, Ogg Vorbis; msvcr71/msvcp71 recompiled with it */
#define DS2_IMAGE_END 0x00d1f000u
#define DS2_TITLE "Grand Theft Auto: San Andreas"
#define DS2_DOCUMENTS "GTA San Andreas User Files"
#define DS2_INI "gta_sa.ini"
#define GAME_EXE "gta_sa.exe"
#endif
#define DS2_LIGHT_LOOP 0xffffff00u
#define DS2_LIGHT_AFTER 0xffffff01u
#define DS2_LIGHT_ZERO 0u
#define DS2_LIGHT_ONE 0u
#define DS2_LIGHT_SCALE 0u
#define DS2_MAT3 0xffffff02u
#define DS2_MAT3_FN fnv_no_mat3
#define DS2_MOOD_READ 0xffffff03u
#define DS2_SKRIT_MANAGER 0u
#elif defined(DF_EDITION)
/* Delta Force (Steam 32620, build 19311) and Delta Force 2 (Steam 32630, build 19312), unwrapped by
 * tools/steamstub21.py: software renderers on DirectDraw; no Dungeon Siege hooks */
#ifdef DF2_EDITION
#define DS2_IMAGE_END 0x00e69000u
#define DS2_TITLE "Delta Force 2"
#define DS2_DOCUMENTS "Delta Force 2"
#define GAME_EXE "Df2.exe"
#else
#define DS2_IMAGE_END 0x00db0000u
#define DS2_TITLE "Delta Force"
#define DS2_DOCUMENTS "Delta Force"
#define GAME_EXE "Df.exe"
#endif
#define DS2_INI "df.cfg"
#define DS2_LIGHT_LOOP 0xffffff00u
#define DS2_LIGHT_AFTER 0xffffff01u
#define DS2_LIGHT_ZERO 0u
#define DS2_LIGHT_ONE 0u
#define DS2_LIGHT_SCALE 0u
#define DS2_MAT3 0xffffff02u
#define DS2_MAT3_FN fnv_no_mat3
#define DS2_MOOD_READ 0xffffff03u
#define DS2_SKRIT_MANAGER 0u
#elif defined(SIMS_EDITION)
/* The Sims Legacy Collection (Steam 3314060, build 17905164, unwrapped by tools/steamstub.py): no Dungeon Siege hooks */
#define DS2_IMAGE_END 0x00b02000u
#define DS2_TITLE "The Sims Legacy Collection"
#define DS2_DOCUMENTS "The Sims"
#define DS2_INI "Sims.ini"
#define GAME_EXE "Sims.exe"
#define DS2_LIGHT_LOOP 0xffffff00u
#define DS2_LIGHT_AFTER 0xffffff01u
#define DS2_LIGHT_ZERO 0u
#define DS2_LIGHT_ONE 0u
#define DS2_LIGHT_SCALE 0u
#define DS2_MAT3 0xffffff02u
#define DS2_MAT3_FN fnv_no_mat3
#define DS2_MOOD_READ 0xffffff03u
#define DS2_SKRIT_MANAGER 0u
#elif defined(FNV_EDITION)
/* Fallout: New Vegas (GOG 1.4.0.525): no Dungeon Siege hooks; the names below are what the host and registry use */
#define DS2_IMAGE_END 0x01409000u
#define DS2_TITLE "Fallout: New Vegas"
#define DS2_DOCUMENTS "FalloutNV"
#define DS2_INI "Fallout.ini"
#define GAME_EXE "FalloutNV.exe"
#define DS2_LIGHT_LOOP 0xffffff00u
#define DS2_LIGHT_AFTER 0xffffff01u
#define DS2_LIGHT_ZERO 0u
#define DS2_LIGHT_ONE 0u
#define DS2_LIGHT_SCALE 0u
#define DS2_MAT3 0xffffff02u
#define DS2_MAT3_FN fnv_no_mat3
#define DS2_MOOD_READ 0xffffff03u
#define DS2_SKRIT_MANAGER 0u
#elif defined(DS2_BROKEN_WORLD)
#define DS2_IMAGE_END 0x01e8a000u
#define DS2_TITLE "Dungeon Siege II: Broken World"
#define DS2_DOCUMENTS "Dungeon Siege 2 Broken World"
#define DS2_INI "DungeonSiege2BrokenWorld.ini"
#define DS2_LIGHT_LOOP 0x00553450u
#define DS2_LIGHT_AFTER 0x00553545u
#define DS2_LIGHT_ZERO 0x00b8f468u
#define DS2_LIGHT_ONE 0x00b8ed44u
#define DS2_LIGHT_SCALE 0x00b8e84cu
#define DS2_MAT3 0x0068e24cu
#define DS2_MAT3_FN f_0068e24c
#define DS2_MOOD_READ 0x00921f86u
#define DS2_SKRIT_MANAGER 0x00d01604u
#else
#define DS2_IMAGE_END 0x01000000u
#define DS2_TITLE "Dungeon Siege II"
#define DS2_DOCUMENTS "Dungeon Siege 2"
#define DS2_INI "DungeonSiege2.ini"
#define DS2_LIGHT_LOOP 0x00544df0u
#define DS2_LIGHT_AFTER 0x00544ee5u
#define DS2_LIGHT_ZERO 0x00a8f338u
#define DS2_LIGHT_ONE 0x00a8ec14u
#define DS2_LIGHT_SCALE 0x00a8e71cu
#define DS2_MAT3 0x0067aeb6u
#define DS2_MAT3_FN f_0067aeb6
#define DS2_MOOD_READ 0x0090a076u
#define DS2_SKRIT_MANAGER 0x00bcb248u
#endif
#ifndef GAME_EXE
#define GAME_EXE "DungeonSiege.exe"
#endif
#endif
