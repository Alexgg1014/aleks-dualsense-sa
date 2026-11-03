<p align="center"><img src="docs/logo.png" alt="Alek's DualSense SA" width="520"></p>

# Alek's DualSense SA

Native **DualSense** support for **GTA San Andreas (PC 1.0 US)**: adaptive triggers for every weapon and vehicle, **HD haptics** played through the controller's own actuators, and a lightbar that reacts to the game. No Steam Input, DSX, DS4Windows or ViGEm needed.

**[Download the latest release](../../releases/latest)**: get the zip under *Assets*, not the source code.

## Features

- **Native input.** The DualSense is read directly over USB or Bluetooth and shown to the whole game as XInput controller 0. GInput, the game and other mods keep working without changes.
- **Bluetooth (2.2).** Wireless works too: input, adaptive triggers, rumble, lightbar and LEDs, with the battery level in the menu. HD haptics need the USB cable, because Windows exposes no haptics audio channel over Bluetooth; wireless uses classic rumble instead.
- **Xbox mode (2.2).** With no Sony pad connected, an Xbox or any XInput controller gets the mod's rumble effects (shots, hits, explosions, crashes, terrain) mixed with the game's own rumble. A DualShock 3 works the same through DsHidMini in XInput mode. `XboxRumble=0` turns it off.
- **DualShock 4 too.** A DualShock 4 over USB or Bluetooth gets native input, rumble and the reactive lightbar (it has no adaptive triggers or HD actuators). Its combo key is **PS** instead of Mute.
- **Adaptive triggers**, different for each weapon:
  - pistols with a trigger click, and the shot fires right at the click,
  - shotguns with a hard pull and a dry kick,
  - **COD-style bolt action** for the country rifle and sniper (slack, a firm wall, a crisp break, then a dead trigger while the bolt cycles),
  - automatics that buzz at their own rate while firing (Uzi, Tec-9, MP5, AK-47, M4), minigun, flamethrower, chainsaw,
  - a bow-like tension for thrown weapons, and a loose trigger when the gun is empty or reloading.
- **Driving triggers**: the throttle stiffens with speed, the brake is firm and judders when you skid (ABS) or pull the handbrake, wheelspin, a coughing engine when the car is about to die. Bikes are lighter, BMX pedals, aircraft have constant tension.
- **HD haptics** through the controller's audio channels 3-4, inspired by how Astro Bot uses the DualSense:
  - a different feel for each gun, reload clicks and the bolt cycle; with an EarShot weapon sound pack (NextGen Weapon Sounds in Immersive Weapons Pack) each shot is **felt from that gun's own sound**, the way Astro Bot derives its haptics from audio (2.2),
  - **footsteps by surface** synced to CJ's walk animation: heel and toe on concrete, soft grains on sand, sharp grit on gravel, a hollow knock on wood, a ringing click on metal, splashes in water,
  - engine pitch that follows the revs, road texture, tyre screech, bumps and landings,
  - **crashes by zone** (front, rear, left or right) and **explosions felt from their side**,
  - molotovs that break like glass.
  If HD haptics are off or unavailable, it falls back to classic rumble.
- **Lightbar and LEDs**: health colour (it beats when you are low), armour on the player LEDs, a speedometer in vehicles, red and blue police flashes when you are wanted.
- **Safe by design**: effects pause in menus, pause, cutscenes and Photo Mode, and the triggers are released when the game stops or closes.
- **In-game menu** in the San Andreas style (Spanish and English), with a TEST tab to feel every trigger and haptic effect.

## Requirements

- GTA San Andreas **1.0 US** with an ASI Loader or Modloader.
- **GInput**: it is what reads the controller for the game. Without it the game does not see the pad.
- A **DualSense**, **DualSense Edge** or **DualShock 4** on a **USB cable** or over **Bluetooth** (paired in Windows settings). HD haptics need the cable. With both connected, the DualSense is used (`Controller` in the INI picks one).
- **Steam Input off** for GTA San Andreas (Steam > Library > right click the game > Properties > Controller).

## Install

1. Download the zip from [Releases](../../releases/latest).
2. With Modloader, copy the `Aleks DualSense SA` folder into `modloader`. Otherwise, copy `Aleks.DualSense.SA.asi` and `Aleks.DualSense.SA.ini` into the game folder.
3. Plug in the controller (or pair it over Bluetooth) and start the game.

**Controls:** Mute + Right opens the menu (or F11). Mute + Up/Down changes the vibration. A tap on Mute turns the adaptive triggers on or off. On a DualShock 4, hold PS instead of Mute. Settings changed in game are saved to `Aleks.DualSense.SA.ini`, the same file you can edit by hand (2.2.1). Full instructions are in `LEEME - README.txt` inside the zip.

If something breaks, open an issue and attach `Aleks.DualSense.SA.log` from the mod folder.

## Building

The mod is a single 32-bit DLL (`.asi`) with no dependencies beyond the Windows SDK. It is built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (i686):

```bat
build.bat
```

This produces `Aleks.DualSense.SA.asi` and runs the core tests (`test_ds.exe`): trigger encodings, input mapping, output report, lights and the haptics synthesizer.

Source layout:

| File | What it holds |
| --- | --- |
| `src/dualsense.cpp` | Entry point, HID device, XInput bridge, game telemetry (weapons, vehicles, explosions, footsteps) |
| `src/ds_core.h` | Pure logic: input mapping, trigger effects, weapon and vehicle profiles, lights, output report |
| `src/ds_haptics.h` | HD haptics synthesizer and recipes (shots, footsteps, glass, crashes) |
| `src/ds_audio.inc` | WASAPI stream to the DualSense's audio endpoint |
| `src/ds_menu.inc` | The in-game menu and translations |
| `src/test_ds.cpp` | Tests for `ds_core.h` and `ds_haptics.h` |
| `src/test_bridge.cpp` | Offline check of the XInput bridge (loads the ASI read-only, outside the game) |

## How it works

- The controller is opened with the Windows HID API (`hid.dll`, `setupapi`). Input report `0x01` (64 bytes) is read on its own thread, and output report `0x02` (48 bytes) carries triggers, lights and rumble.
- `XInputGetState/SetState/GetCapabilities` are patched at their entry in every loaded `xinput*.dll`, so every reader in the process gets the same controller. Only known prologues are patched, other mods' hooks are chained, and the trampolines are never writable and executable at the same time.
- Game state is read on the game thread. A watchdog releases the triggers if the game stops answering.
- HD haptics leave the rumble-emulation bits clear in the output report, so the actuators follow the audio endpoint's back channels. The waveforms are synthesized in real time; no sound files are shipped.

Every game address is for GTA SA 1.0 US and was checked against [DK22Pac's plugin-sdk](https://github.com/DK22Pac/plugin-sdk) and the game binary.

## Credits & inspiration

- **Native DualSense for GTA IV** ([Nexus Mods #1480](https://www.nexusmods.com/gta4/mods/1480)) and **DualSense Features for GTA IV** by **NodixTech** ([Nexus Mods #1136](https://www.nexusmods.com/gta4/mods/1136)): the inspiration. Their features and presets set the target for San Andreas. No code or assets from them are included.
- **Astro's Playroom** and **Astro Bot** by Team Asobi, the reference for how the haptics should feel ([GDC 2025: Feel the World](https://gdcvault.com/play/1035347/Feel-the-World-The-DualSense)).
- The DualSense protocol follows the Linux [`hid-playstation`](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c) driver and SDL's [HIDAPI PS5 driver](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_ps5.c). The trigger effect formats follow **Nielk1**'s [TriggerEffectGenerator](https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db) (MIT).
- [DK22Pac's plugin-sdk](https://github.com/DK22Pac/plugin-sdk) and [gta-reversed](https://github.com/gta-reversed/gta-reversed) for the documented addresses, structures and the game's footstep timing.
- Developed with the help of **Claude** (Anthropic's AI) for the coding, reverse engineering and debugging. Design, testing and every in-game check were done by Alexgg1014.

## License

[MIT](LICENSE). You can use, modify and share it; keep the copyright notice and give credit.

GTA San Andreas is a trademark of Rockstar Games. DualSense is a trademark of Sony Interactive Entertainment. This is a fan-made mod, not affiliated with Rockstar Games, Take-Two Interactive or Sony.
