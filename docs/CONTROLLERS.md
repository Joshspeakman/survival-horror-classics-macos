# Controllers

Any controller macOS knows (Xbox, PlayStation, Switch Pro, MFi) works as soon as it is connected, also one turned on
after the game started. The controller used last is the one read, 250 times a second, while the game is the active
app.

The games read a DirectInput gamepad and come with a default joypad layout made for the PS2's buttons; the controller
is presented to them in that order, so it plays like the PS2 versions:

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

(The exact actions differ a little between the three games, as on the PS2; each game's Controller Setup shows and
changes its joypad layout.) The triggers are buttons, as on the PS2. Menu is the Escape key, as the PC versions can't
put the pause menu on a pad button.

How it works: the host reads the controller with Apple's GameController framework (`recomp/host/main.m`) and the
runtime's DirectInput 8 (`recomp/runtime/win32/dinput8.c`) reports it as a gamepad that is always attached; its axes
are signed (−32768..32767), as the games read them without setting a range. `W32_NO_PAD=1` hides it.
