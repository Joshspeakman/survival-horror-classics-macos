# Controllers

Any controller macOS knows (Xbox, PlayStation, Switch Pro, MFi) works as soon as it is connected, also one turned on
after the game started. The controller used last is the one read, 250 times a second, while the game is the active
app.

The games read a DirectInput gamepad and come with a default joypad layout made for the PS2's buttons; the controller
is presented to them in that order. The launcher's **Controls** row picks the layout: **Classic** plays like the PS2
versions, **Modern** like the Definitive Edition's Modern scheme.

## Classic (PS2)


| Controller | PS2 button | Main uses (all three games) |
|---|---|---|
| Left stick | left stick | move, steer |
| Right stick | right stick | camera and look |
| A | Cross (X) | sprint; accelerate |
| B | Circle | fire, attack |
| X | Square | jump; brake and reverse |
| Y | Triangle | enter and leave vehicles |
| LB | L1 | (game-specific: action, look, horn) |
| RB | R1 | target; handbrake |
| LT / RT | L2 / R2 | weapon switch; look left and right |
| View | Select | camera mode |
| Left stick click | L3 | (game-specific: crouch, horn) |
| Right stick click | R3 | look behind |
| D-pad | D-pad | menus, radio stations, mission and conversation choices |
| Menu | Start | the pause menu |

## Modern (Definitive Edition)

Laid out as the Definitive Edition's Modern scheme (its Xbox control tables). The games themselves only know the PS2
pad, so each button is given the PS2 button its action is on, by where the player is: on foot, in a vehicle, on a
bicycle or in an aircraft (San Andreas), read from the game (the player's in-vehicle flag and vehicle class). Menus keep
the PS2's layout.

| Controller | On foot | Cars, motorbikes, boats | Bicycles (San Andreas) | Aircraft (San Andreas) |
|---|---|---|---|---|
| RT | attack | accelerate | — | ascend, thrust |
| LT | aim, target | brake, reverse | — | descend |
| RB | phone, swap weapons, reset camera | handbrake | handbrake | rudder right |
| LB | next weapon | fire | bunny hop (hold: higher) | rudder left |
| A | run (hold), sprint (tap) | nitrous | pedal (hold), pedal faster (tap) | main weapon |
| B | attack | look right | fire | other weapon |
| X | jump, climb | look left | brake | — |
| Y | enter a vehicle | leave | leave | leave |
| Left stick click | crouch | horn, siren | bell | landing gear |
| Right stick click | look behind | look behind | look behind | look behind |
| D-pad up | (as the PS2) | sub-missions | (as the PS2) | sub-missions |
| D-pad right | (as the PS2) | radio station | (as the PS2) | radio |

As in the Definitive Edition, a bicycle is pedalled with A: holding it pedals, tapping it pedals faster, as the
originals' tap-to-go-faster works (the trigger would make that a hold). The Definitive Edition's weapon and radio wheels
have no counterpart in these games: LB steps to the next weapon, the D-pad changes station.

## Notes

(The exact actions differ a little between the three games, as on the PS2; each game's Controller Setup shows and
changes its joypad layout.) The triggers are buttons, as on the PS2. Menu is the Escape key, as the PC versions can't
put the pause menu on a pad button.

Vice City's car camera leans into turns by the steering times the speed. With a keyboard, which turns the wheel a little
at a time, the lean stays slight; a stick is at full lock at once, so the camera banked hard in every turn. The lean is
kept off (`GTA_CAR_ROLL=1` brings it back).

How it works: the host reads the controller with Apple's GameController framework (`recomp/host/main.m`) and the
runtime's DirectInput 8 (`recomp/runtime/win32/dinput8.c`) reports it as a gamepad that is always attached; its axes
are signed (−32768..32767), as the games read them without setting a range. `W32_NO_PAD=1` hides it.
