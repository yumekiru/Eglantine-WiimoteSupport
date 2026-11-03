# WiimoteSupport

Local Eglantine/Lichii branch: `wiimote-motionplus`, based on `3bdd53d08adfd96b7e3e3f09344c514540ef1ce6`.

## Start

Close Dolphin and other software controlling the DolphinBar. Leave the bar in mode 4 and connect both remotes. Start `Play/lichii.exe`, or use the game shortcut in this folder. Player 1 is already configured as a detached Joy-Con pair; other players are disconnected.

LED 1 is the left hand. LED 2 is the right hand. Both remotes belong to one player. Keep them still during the game's calibration steps. In Emulation > Configure > Controls, the device is `Wii Remote Pair 1 (Left + Right)`. The individual remotes and `Wiimote-Left` / `Wiimote-Right` profiles support single Joy-Con configurations. A single profile must be selected with the matching emulated Joy-Con type.

## Buttons

| Switch control | Physical remote/button |
|---|---|
| A | Right A |
| B | Right B |
| X / Y | Right + / - |
| L / ZL | Left 1 / 2 |
| R / ZR | Right 1 / 2 |
| Start (Switch +) | Right Home |
| Left analog stick | Left D-pad in pair mode; active remote D-pad in single mode |
| Right analog stick | Right D-pad in pair mode |
| SL / SR | 1 / 2 on the corresponding remote |

For an L+R prompt, press 1 on each remote together. For SL+SR on one remote, press 1+2 on that remote. SL/SR overlap some other controls because a Wii Remote has fewer buttons than a Joy-Con. Controls remain remappable in the emulator. D-pad directions become full-strength analog-stick input, with centered release and normalized diagonals. A physical D-pad cannot provide a gradual analog range. Digital Switch D-pad buttons are left unbound so a direction does not trigger both inputs.

## Verified

- The portable Windows emulator compiled and linked successfully.
- Actual controller tests received two simultaneous, independent MotionPlus streams. The final test counted 3,570 samples from the left remote and 3,495 from the right over 20 seconds.
- The packet decoder tests passed for D-pad directions, cancellation and diagonal normalization, gravity scale, gyro axis signs and units, slow/fast range switching, report offsets, and invalid/truncated packets.
- The supplied ExeFS executable launched and rendered its intro at approximately 60 FPS. Transport testing and an intro rendering do not establish that every sport or movement behaves correctly.

Run `Play/wiimote_diagnostic.exe` with the emulator and Dolphin closed to repeat the hardware check. Gently rotate each remote during its 20-second capture. It reports success only after receiving valid MotionPlus data from both remotes. `Play/wiimote_protocol_test.exe` repeats the deterministic decoder tests.

## Gyro calibration (v0.2.0)

The build now measures each remote's gyro zero after two seconds of stable motion. Leave both remotes still on a table at startup until the player LEDs stop flashing. Detected movement restarts the measurement window. Keep the remote completely still; steady rotation cannot always be distinguished from sensor bias without an external reference. Reconnecting repeats this measurement, so calibration is not tied to a particular bar slot or borrowed from the other remote.

Bias is removed from raw sensor counts before fast/slow range scaling. The Wiimote-specific motion cutoff is reduced from 2.52 degrees/second to 0.18 degrees/second, retaining fine aiming movement while remaining above the measured stationary noise. Explicit custom motion thresholds still take precedence.

`wiimote_diagnostic.exe --stationary` performs a 20-second rest test. In this setup the worst mean stationary rate fell from approximately 30 degrees/second to 0.003 degrees/second. Numeric decoder/calibration tests and an actual emulator motion-filter regression both pass. The owner reported improved archery control after this update, but aiming remains difficult. This is an experimental improvement, not a claim that archery is fully fixed. The v0.1.0 baseline does not include these corrections.
## Implementation and limits

The direct driver owns DolphinBar interfaces 0 and 1, activates integrated MotionPlus without reading peripheral memory, decodes accelerometer and gyro reports, and sends each hand separately to the emulator's existing motion processing. Ordinary Wii rumble is supported. Device disconnects clear buttons and motion; the worker scans for reconnections.

This specifically targets the Mayflash DolphinBar and the connected Motion 2-in-1 remotes. SDL's Wii backend is disabled to prevent its memory-read initialization from disrupting these clones. Other SDL controllers remain supported. The Wii button-to-Joy-Con mappings and input-device selection also preserve distinct ports when both devices share a GUID.

The decoder uses typical accelerometer calibration and nominal MotionPlus scales because these clones do not reliably return factory calibration through the bar. Bias, sensitivity, physical axis alignment, drift and sport-specific behavior require in-game calibration and testing. HD rumble, Joy-Con IR/NFC, physical analog-stick precision, and identical physical sensor characteristics cannot be supplied by these Wii Remotes. This is not a claim of perfect hardware equivalence.

Protocol references: [Dolphin](https://github.com/dolphin-emu/dolphin), [SDL Wii backend](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_wii.c), and [WiiChinaHook's clone/DolphinBar findings](https://github.com/angelopol/WiiChinaHook). The reference sources are research material; the new backend does not require those programs to run.

## Build notes

Build with `tools/windows/build-wiimote.ps1`. The local Visual Studio 2022 compiler is compatible with the source, but the repository's bundled Qt requires newer STL runtime symbols. The script uses the installed Visual Studio 2026 STL import library when present. It also applies local overrides for three upstream unused-function/variable diagnostics. The ordinary project build guide remains in `docs/Build.md`.

Portable binaries and Dolphin are excluded locally through `.git/info/exclude`. The clone has an upstream `dist/Eglantine.icns` / `dist/eglantine.icns` filename collision on Windows; neither icon is part of the controller implementation. This fork was developed with AI assistance (Codex), reviewed through packet tests and live controller tests, and tested interactively by the owner. No upstream pull request is included.
