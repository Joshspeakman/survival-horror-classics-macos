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

The sticks work as in today's games: the left stick moves (toward where it points, seen from the camera) and the right
stick turns the camera (up looks up).

- **Right stick: the camera.** It drives each game's own mouse camera, a free orbit camera that also aims freely: on
  foot in all three, and in San Andreas' vehicles (not aircraft, where the mouse would fly). San Andreas' own *Steer
  with Mouse* setting is held off under Modern, since the stick reaches the game as the mouse and would steer.
- **GTA III's and Vice City's car camera.** The PC games have no free camera in vehicles. With Modern, the right stick
  sideways swings the camera round the car, and once it's let go the camera drifts back behind as the car moves (their
  car camera is a "string" that is pulled along behind the car); down looks behind.
- **Look Speed and Invert Look** (launcher rows, kept for each game): the camera's speed (Slow, Normal, Fast, Very
  Fast: `GTA_STICK_LOOK` 900, 1400, 2000, 2800 counts a second; the games' own mouse sensitivity also applies) and up
  and down the other way round (`GTA_STICK_INVERT=1`). Invert also applies to Classic, where GTA III's and Vice City's
  own stick look has up looking down.
- **Left stick: moving.** GTA III's PC controls turn the player with left and right when the mouse camera is on; with
  Modern it moves him as on the PS2, toward the stick's direction from the camera (while aiming with LT, the PC
  control keeps him facing where the camera looks). Vice City and San Andreas already move that way.

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

Rumble: the PC games never ask DirectInput for force feedback (those calls are logged once if one ever does), so the
controllers don't rumble.

The display stays on while a game is in front: a controller gives macOS no keyboard or mouse activity, so with a pad
alone the display could otherwise go dark mid-game. It sleeps as usual while another app is in front.

How it works: the host reads the controller with Apple's GameController framework (`recomp/host/main.m`) and the
runtime's DirectInput 8 (`recomp/runtime/win32/dinput8.c`) reports it as a gamepad that is always attached; its axes
are −32768..32767 until the game sets a range. The games ask DirectInput about each axis (GetObjectInfo) and then set
it to −2000..2000, which their stick code assumes (× 0.0005, then a 0.3 dead zone); before GetObjectInfo answered, the
sticks came in 16 times too strong, so the slightest push up or down while steering counted as a full one.
`W32_NO_PAD=1` hides it.
