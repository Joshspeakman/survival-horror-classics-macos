#!/usr/bin/env python3
"""gta_qa.py <gta3|vc|sa> <plan> [--out DIR] [--mode WxH] [--msaa N] [--light ps2|pc] [--keep]

Effects QA: runs a game in test mode (DS_TEST, background window, sound muted) into its first gameplay scene, then
goes through a plan of scenes, each set up with the game's own cheats (typed), game memory held at a value (effect
flags, the clock) and pad input, and takes a screenshot of each (DS_SHOT_FINAL: as shown). Writes DIR/NN_name.png,
DIR/index.txt (the scenes, the frame rate around each, crashes) and DIR/game.log. Plans are below (PLANS); the
addresses are this project's builds' (Steam executables: GTA III 1.1 Mac, Vice City Windows 1.1, San Andreas 3.0).

The games go by their own clocks, so the timings are generous; a scene's 'wait' is how long it settles before its shot.
"""
import os, sys, time, subprocess, shutil, argparse, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WORK = os.path.join(ROOT, 'work')
GAMES = {
    'gta3': dict(bin='gta3/app/GTA3', exe='gta3/gta3.exe', game='~/Games/GTA3Native/game-files/GTAIII'),
    'vc': dict(bin='gtavc/app/GTAVC', exe='gtavc/gta-vc.exe', game='~/Games/GTAVCNative/game-files/Grand Theft Auto Vice City'),
    'sa': dict(bin='gtasa/app/GTASA', exe='gtasa/gta_sa.exe', game='~/Games/GTASANative/game-files/GTA San Andreas'),
}

# Getting into play from a fresh start (no save): Return through the menus (Start Game, New Game), the opening
# cutscene, then the first scene. (seconds from launch, command)
INTRO = {
    'sa': [(30 + 6 * i, 'key:13') for i in range(6)] + [(92, None)],                    # CJ in the Grove Street alley by the BMX
    'vc': [(25 + 5 * i, 'key:13') for i in range(5)] + [(62, 'key:70'), (72, None)],   # Tommy in the Admiral
    'gta3': [(25 + 5 * i, 'key:13') for i in range(5)] + [(70, None)],                  # after the bridge cutscene, in the car
}

def hold(*pairs): return 'pin:' + ','.join('%x=%x' % p for p in pairs)
def unhold(*addrs): return 'unpin:' + ','.join('%x' % a for a in addrs) + '\npoke:' + ','.join('%x=0' % a for a in addrs)   # and back to 0

# San Andreas: CClock hours 0xb51c7b; CPostEffects flags 0xc30310 night vision, 0xc30311 infrared vision
SA_HOUR, SA_NV, SA_IR = 0xb51c7b, 0xc30310, 0xc30311
PLANS = {
    'sa-effects': [
        # (name, commands, seconds to settle, commands after the shot)
        ('morning', [], 3, []),
        ('noon very sunny (heat haze)', ['cheat:ICIKPYH', hold((SA_HOUR, 13))], 10, []),
        ('noon overcast', ['cheat:ALNSFMZO'], 8, []),
        ('rain', ['cheat:AUIFRVQS'], 10, []),
        ('fog', ['cheat:CFVFGMJ'], 10, []),
        ('sandstorm', ['cheat:CWJXUOC'], 10, []),
        ('sunny again', ['cheat:AFZLLQLL'], 8, []),
        ('dusk', [hold((SA_HOUR, 20))], 8, []),
        ('night', [hold((SA_HOUR, 23))], 8, []),
        ('night vision (night)', [hold((SA_NV, 1))], 4, [unhold(SA_NV)]),
        ('infrared (night)', [hold((SA_IR, 1))], 4, [unhold(SA_IR)]),
        ('night vision (day)', [hold((SA_HOUR, 13), (SA_NV, 1))], 6, [unhold(SA_NV)]),
        ('infrared (day)', [hold((SA_IR, 1))], 4, [unhold(SA_IR)]),
        ('day again', [], 3, []),
    ],
    'sa-places': [   # teleports (tp:, the game's own CEntity::Teleport); 8 s for the area to stream in
        ('desert noon (heat haze)', ['cheat:ICIKPYH', hold((SA_HOUR, 13)), 'tp:-750,1980,60'], 10, []),
        ('Mount Chiliad view (distance haze)', ['tp:-2330,-1630,484'], 10, []),
        ('Santa Maria beach (water)', ['tp:370,-1900,8'], 10, []),
        ('downtown Los Santos', ['tp:1480,-1730,14'], 10, []),
        ('San Fierro', ['tp:-2000,150,28'], 10, []),
        ('Las Venturas strip at night', [hold((SA_HOUR, 22)), 'tp:2040,1340,11'], 10, []),
        ('Los Santos at night', ['tp:1480,-1730,14'], 10, []),
        ('Grove Street rain at night', ['cheat:AUIFRVQS', 'tp:2495,-1670,14'], 12, []),
    ],
    'sa-tp': [('before', [], 2, []), ('downtown Los Santos', ['tp:1480,-1730,14'], 10, []), ('Santa Maria beach', ['tp:370,-1900,8'], 10, [])],
    # round 2: weather set directly (gta:weather), the player's vehicle driven fast (gta:vspeed), set on fire and blown up
    # (gta:vhealth); peek checks the vehicle's health offset (1000.0 = 0x447a0000)
    'vc-round2': [
        ('start (in the Admiral)', ['peek:949d30,3a8,204'], 3, []),
        ('hurricane', ['gta:weather,5'], 8, []),
        ('rain', ['gta:weather,2'], 6, []),
        ('fog', ['gta:weather,3'], 6, []),
        ('extra sunny noon', ['gta:weather,4', hold((0xa0fb75, 12))], 6, []),
        ('driving fast (trails)', ['gta:vspeed,0.7,240'], 2, []),
        ('engine on fire', ['gta:vhealth,200'], 4, []),
        ('explosion', ['gta:vhealth,0'], 3, []),
        ('after the explosion', [], 4, []),
        ('night', [hold((0xa0fb75, 23))], 6, []),
    ],
    'gta3-round2': [
        ('start', [], 2, []),
        ('in the car', ['key:70'], 4, ['peek:952628,310,200']),
        ('rain', ['gta:weather,2'], 6, []),
        ('fog', ['gta:weather,3'], 6, []),
        ('sunny noon', ['gta:weather,0', hold((0x96e0df, 12))], 6, []),
        ('driving fast', ['gta:vspeed,0.7,240'], 2, []),
        ('engine on fire', ['gta:vhealth,200'], 4, []),
        ('explosion', ['gta:vhealth,0'], 3, []),
        ('after the explosion', [], 4, []),
        ('night', [hold((0x96e0df, 23))], 6, []),
    ],
    'sa-round2': [
        ('start', [], 2, []),
        ('a car (cheat)', ['cheat:CQZIJMB'], 3, []),
        ('in the car', ['key:70'], 4, ['peek:b64788,58c,4c0']),
        ('rain', ['gta:weather,8'], 8, []),
        ('fog', ['gta:weather,9'], 8, []),
        ('sandstorm', ['gta:weather,19'], 8, []),
        ('extra sunny noon', ['gta:weather,0', hold((SA_HOUR, 13))], 8, []),
        ('driving fast (speed blur)', ['gta:vspeed,0.8,300'], 2, []),
        ('engine on fire', ['gta:vhealth,200'], 4, []),
        ('explosion', ['gta:vhealth,0'], 3, []),
        ('after the explosion', [], 4, []),
        ('beach and sea', ['tp:330,-1960,4'], 10, []),
        ('night by the sea', [hold((SA_HOUR, 23))], 6, []),
    ],
    'gta3-veh-find': [('in the car', ['key:70'], 5, ['peek:952628,310', 'dump:3b0b0e0,400,{out}/veh.bin'])],
    'gta3-fire': [('in the car', ['key:70'], 5, []), ('sunny noon', ['gta:weather,0', hold((0x96e0df, 12))], 4, []),
                  ('engine fire (parked)', ['gta:vhealth,200'], 3, []), ('engine fire later', [], 3, []), ('explosion', ['gta:vhealth,0'], 2, [])],
    'vc-fire': [('start', [], 2, []), ('sunny noon', ['gta:weather,0', hold((0xa0fb75, 12))], 4, []),
                ('engine fire (parked)', ['gta:vhealth,200'], 3, []), ('engine fire later', [], 3, []), ('explosion', ['gta:vhealth,0'], 2, [])],
    'sa-speed': [('Las Venturas airport', ['gta:weather,0', hold((SA_HOUR, 13)), 'tp:1477,1500,11'], 8, []),
                 ('a car (cheat)', ['cheat:CQZIJMB'], 3, []), ('in the car', ['key:70'], 4, ['peek:b64788,58c,4c0']),
                 ('driving fast (speed blur)', ['gta:vspeed,0.8,400'], 2, ['framelog']), ('still fast', [], 1, []),
                 ('engine on fire', ['gta:vhealth,200'], 3, []), ('explosion', ['gta:vhealth,0'], 2, []), ('after', [], 3, [])],
    # finding the weather: memory around the clock before and after a weather cheat
    'vc-weather-find': [('before', ['dump:a0e000,3000,{out}/before.bin'], 2, []), ('rain', ['cheat:CATSANDDOGS'], 3, ['dump:a0e000,3000,{out}/after.bin'])],
    'gta3-weather-find': [('before', ['dump:96c000,4000,{out}/before.bin'], 2, []), ('rain', ['cheat:ILOVESCOTLAND'], 3, ['dump:96c000,4000,{out}/after.bin'])],
    'sa-haze': [('noon very sunny', ['cheat:ICIKPYH', hold((SA_HOUR, 13))], 12, ['framelog']), ('after', [], 3, [])],
    # Vice City: CClock hours 0xa0fb75; weather cheats
    'vc-effects': [
        ('start (in the Admiral)', [], 3, []),
        ('rain', ['cheat:CATSANDDOGS'], 10, []),
        ('fog', ['cheat:CANTSEEATHING'], 10, []),
        ('overcast', ['cheat:ABITDRIEG'], 8, []),
        ('sunny noon', ['cheat:ALOVELYDAY', hold((0xa0fb75, 12))], 8, []),
        ('dusk', [hold((0xa0fb75, 19))], 8, []),
        ('night', [hold((0xa0fb75, 23))], 8, []),
        ('night rain', ['cheat:CATSANDDOGS'], 10, []),
        ('dawn', ['cheat:ALOVELYDAY', hold((0xa0fb75, 6))], 8, []),
    ],
    # GTA III: CClock hours 0x96e0df; weather cheats
    'gta3-effects': [
        ('start (night, in the car)', [], 3, []),
        ('rain', ['cheat:ILOVESCOTLAND'], 10, []),
        ('fog', ['cheat:PEASOUP'], 10, []),
        ('cloudy', ['cheat:ILIKESCOTLAND'], 8, []),
        ('sunny noon', ['cheat:SKINCANCERFORME', hold((0x96e0df, 12))], 8, []),
        ('dusk', [hold((0x96e0df, 19))], 8, []),
        ('night', [hold((0x96e0df, 23))], 8, []),
        ('dawn', [hold((0x96e0df, 6))], 8, []),
    ],
    # San Andreas: each byte of CPostEffects' flag block held at 1 in turn (finding the effects' switches)
    'sa-flags': [('flag %x' % a, [hold((a, 1))], 3, [unhold(a)]) for a in range(0xc30310, 0xc30334)],
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', choices=sorted(GAMES)); ap.add_argument('plan', choices=sorted(PLANS))
    ap.add_argument('--out', default=None); ap.add_argument('--mode', default='1600x1000'); ap.add_argument('--msaa', default='2')
    ap.add_argument('--light', default='ps2'); ap.add_argument('--keep', action='store_true', help='keep the run\'s data folder')
    a = ap.parse_args()
    g = GAMES[a.game]; out = os.path.abspath(a.out or os.path.join(WORK, 'qa', a.game + '-' + a.plan))
    shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
    data = os.path.join(out, 'data'); os.makedirs(data); cmd = os.path.join(out, 'cmd.txt'); open(cmd, 'w').close()
    env = dict(os.environ, DS_TEST='1', DS_NO_LAUNCHER='1', W32_AUDIO_MUTE='1', GTA_NO_MOVIES='1', DS_SHOT_FINAL='1', DS_CMDFILE=cmd,
               GTA_MODE=a.mode, GTA_MSAA=a.msaa, D3D9_FRAMELOG_FILE=os.path.join(out, 'framelog.go'), GTA_LIGHTING=a.light, GTA_WIDESCREEN='1', GTA_FPS='60')
    log = open(os.path.join(out, 'game.log'), 'w')
    p = subprocess.Popen([os.path.join(WORK, g['bin']), '--exe', os.path.join(WORK, g['exe']), '--game', os.path.expanduser(g['game']),
                          '--data', data], env=env, stdout=log, stderr=subprocess.STDOUT)
    t0 = time.time()
    def at(t):
        d = t - (time.time() - t0)
        if d > 0: time.sleep(d)
    def send(*lines):
        for l in lines:
            if l == 'framelog':                       # this frame's draws into game.log (D3D9_FRAMELOG_FILE: once, as the file appears)
                open(os.path.join(out, 'framelog.go'), 'w').close(); continue
            if l.startswith('cheat:'):               # a cheat: its letters as key presses, held a few frames each, 0.3 s apart (the host reads the file every 15 frames)
                for ch in l[6:].upper():
                    with open(cmd, 'a') as f: f.write('key:%d\n' % ord(ch))
                    time.sleep(0.3)
            else:
                with open(cmd, 'a') as f: f.write(l.replace('{out}', out) + '\n')
    for t, c in INTRO[a.game]:
        at(t)
        if c: send(c)
    shots = []; t = time.time() - t0
    for k, (name, before, wait, after) in enumerate(PLANS[a.plan]):
        send(*before); t += wait; at(t)
        png = os.path.join(out, '%02d_%s.png' % (k, re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')))
        send('shot:' + png); t += 1.5; at(t)
        send(*after); shots.append((name, png, time.time() - t0))
    at(t + 2); p.kill(); p.wait(); log.close()
    text = open(os.path.join(out, 'game.log'), errors='replace').read()
    fps = [(float(m.group(1)), float(m.group(2))) for m in re.finditer(r'frame (\d+) .*? ([\d.]+) fps;', text)]
    crash = re.findall(r'unhandled exception.*', text)
    with open(os.path.join(out, 'index.txt'), 'w') as f:
        f.write('%s %s, %s, MSAA %s, %s lighting\n' % (a.game, a.plan, a.mode, a.msaa, a.light))
        for n, (name, png, ts) in enumerate(shots):
            f.write('%2d  %-34s %s%s\n' % (n, name, os.path.basename(png), '' if os.path.exists(png) else '  (no picture)'))
        f.write('crashes: %s\n' % (crash or 'none'))
        if fps: f.write('frame rate: min %.1f, last %.1f\n' % (min(r for _, r in fps[3:] or fps), fps[-1][1]))
    if not a.keep: shutil.rmtree(data, ignore_errors=True)
    print(open(os.path.join(out, 'index.txt')).read(), end='')

if __name__ == '__main__':
    main()
