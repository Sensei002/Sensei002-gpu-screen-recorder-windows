# About
Global hotkeys for X11 and all Wayland compositors by using linux device api. Keyboards are grabbed and only the non-hotkey keys are passed through to the system.
The program accepts text commands as input. Run the program with the option `--virtual` to only grab virtual devices. This is useful when using keyboard input mapping software such as
kanata, otherwise kanata may fail to launch or this program may fail to launch.
# Commands
## Bind
To add a key send `bind <action> <keycode+keycode+...><newline>` to the programs stdin, for example:
```
bind show_hide 56+44

```
which will bind alt+z. When alt+z is pressed the program will output `show_hide` (and a newline) to stdout.
The program only accepts one key for each keybind command but accepts a multiple modifier keys.
The keybinding requires at least one modifier key (ctrl, alt, super or shift) and a key to be used.
The keycodes are values from `<linux/input-event-codes.h>` linux api header (which is the same as X11 keycode value minus 8).
## Unbind
To unbind all keys send `unbind_all<newline>` to the programs stdin, for example:
```
unbind_all

```
## Exit
To close gsr-global-hotkeys send `exit<newline>` to the programs stdin, for example:
```
exit

```
# Conflict with other keyboard software
Some keyboard remapping software such as keyd may conflict with gsr-global-hotkeys if configured incorrect (if it's configured to grab all devices, including gsr-ui virtual keyboard).
If that happens it may grab gsr-ui-virtual keyboard while gsr-global-hotkeys will grab the keyboard remapping software virtual device, leading to a circular lock, making it not possible
to use your keyboard. gsr-global-hotkeys detects this and outputs `gsr-ui-virtual-keyboard-grabbed` to stdout. You can listen to this and stop gsr-global-hotkeys or restart it with `--no-grab`
option to only listen to devices, not grabbing them.