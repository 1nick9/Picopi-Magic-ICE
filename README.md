Top note i vibe coded this port, wasnt as simple as feed it the code and done, had to really guide it. but anyway got a good working f=tr branch, no others been confirmed figure post as is once have 100% working f=tr branch on console tested. This would have taken me for ever on own. really great things, left the readme as it summaried for posting. any way now have a modulared off code base for picopi to work with :)

the note on f=tr is wrong, it drives the console reset trigger than 2N7002.


# ps2-ice-pico

A port of the **H2O-Improved / Magic ICE** PS2 modchip firmware from the
Ubicom/Parallax **SX28** to the **RP2040** (Raspberry Pi Pico and similar
boards), written in C with PIO for the bus work.

The source of truth is the reverse-engineered SX28 assembly
[`h2o-sx28ORsx48-compile-working.s`](https://github.com/1nick9/PS2-Magic-ICE-FINAL-REVERSE)
from the PS2-Magic-ICE-FINAL-REVERSE project. The port follows that code
routine by routine: same syncs, same strobe counts, same patch data, same
flow between routines. Comments in the C sources quote the asm labels they
implement.

> **Status: only the F=TR build on a Waveshare RP2040-Tiny has been tested
> and confirmed working** (JAP v7/v8 console, BIOS rev 1.60).
> RSTBUMP, H2O75KJMPERS, the flip-flop wiring, the other boards and the
> fixed-region builds compile, but have not been tested on a console.

## Features (as in the H2O SX28 code)

- PS2: OSDSYS patch, xcdvdman `CdCheck` patch (repatched on every reload),
  PS2 logo patch, mechacon region / media unlock (v1–v8 and v9–v14 paths)
- PS1: SCEx injection, PS1 logo patch, PAL Y-fix, NTSC Y-fix on 75k PAL
  consoles (`NTSCPS1YFIX75K`)
- DVD: macrovision off / green fix
- Mode select at power-on: PS2, PS1, DEV1 (boots `mc0:/BOOT/BOOT.ELF`),
  disable
- Auto-reset into PS1 mode when a PS2 logo is not found (F=TR or RSTBUMP)
- Sleep / wake on BIOS CS, reset and tray edges, as the SX28 does

## Tested

| Item | Result |
|---|---|
| Board | Waveshare RP2040-Tiny, 240 MHz |
| Console | JAP v7/v8 (BIOS rev 1.60, year 2002), mechacon v1–v8 path |
| Build | F=TR (RSTBUMP off, H2O75KJMPERS off), H2O dev1, DIRECT mechacon OE |
| PS2 originals / backups, DVD unlock | working |
| PS1 mode (SCEx, PS1 logo) | working |
| DEV1 (`mc0:/BOOT/BOOT.ELF`) | working |
| Disable mode until standby | working |

Series resistors used on the tested install:

| Point | Resistor |
|---|---|
| RESET | 33 kΩ |
| R (BIOS /OE) | 3.3 kΩ |
| W (BIOS /CE) | 3.3 kΩ |
| A (mechacon OE) | 1.2 kΩ |

A JAP v5/v6 console (BIOS rev 1.60, year 2001) has not unlocked DVDs in
testing so far; it was tested only with RSTBUMP + H2O75KJMPERS builds and
on a YD-RP2040, and is still to be retested with the F=TR build.

## Prebuilt firmware

`prebuilt/` holds UF2 files: hold BOOTSEL, plug in USB, copy the file.
All are the F=TR build (RSTBUMP off, H2O75KJMPERS off, NTSCPS1YFIX75K on,
H2O dev1, DIRECT mechacon OE unless noted).

| Folder | Region handling |
|---|---|
| `prebuilt/` | from the BIOS (normal) |
| `prebuilt/v8jap/` | `JAPv14orv8`: fixed JAP 75k region; v7/v8 JAP consoles use the v9+ mechacon patch |
| `prebuilt/usa/` | `USAv14`: fixed USA 75k region |

| File | Board | Debug log |
|---|---|---|
| `tiny_dbg` / `tiny_nodbg` | Waveshare RP2040-Tiny | on / off |
| `pico_dbg` / `pico_nodbg` | Raspberry Pi Pico | on / off |
| `pico_ff` | Raspberry Pi Pico, 74HC73 flip-flop wiring | on |
| `yd16_dbg` / `yd16_nodbg` | YD-RP2040, 16 MB | on / off |
| `yd4_dbg` | YD-RP2040, 4 MB | on |

## Wiring

Letters are the usual Magic ICE / H2O install points.

| Signal | Point | Pico / YD-RP2040 | RP2040-Tiny |
|---|---|---|---|
| BIOS D0..D7 | V U T Q P O N M | GP0..GP7 | GP0..GP7 |
| BIOS /CE (CS) | W | GP8 | GP8 |
| BIOS /OE | R | GP9 | GP9 |
| RESET | RST | GP10 | GP26 |
| EJECT (tray) | Z | GP11 | GP27 |
| SCEx | S | GP12 | GP28 |
| Mechacon F (F=TR: 2N7002 gate) | F | GP13 | GP10 |
| Mechacon B, G, H, I | B G H I | GP14..GP17 | GP11..GP14 |
| Mechacon OE (DIRECT) / flip-flop /1Q | A | GP18 | GP15 |
| Flip-flop /1R (FLIPFLOP builds) | — | GP19 | GP29 |
| Status LED | — | GP25 | — |

- **F=TR:** pad F drives the gate of a 2N7002 (with a gate pull-down) whose
  drain pulls the console RESET; used for the auto-reset into PS1 mode.
- **v1–v8 consoles:** G is not needed. H is needed on JAP consoles.
- **DIRECT** (default): A goes straight to a GPIO; the PIO does the job of
  the 74HC73 flip-flop of the original board. **FLIPFLOP**: original board
  wiring with the 74HC73.
- Pin maps are in `include/boards/ice_board_*.h`; B, G, H, I must be four
  consecutive GPIOs.

## Building

Requirements: Pico SDK 2.x, arm-none-eabi-gcc, CMake, Ninja (or the
Raspberry Pi Pico VS Code extension, which sets all of this up).

```
cmake -S . -B build -G Ninja -DICE_BOARD=waveshare_rp2040_tiny
cmake --build build
```

In VS Code: open the folder, pick the board in the `ICE_BOARD` line of
`CMakeLists.txt` (or in the CMake cache), then Compile.

| Option | Values | Default |
|---|---|---|
| `ICE_BOARD` | `pico`, `waveshare_rp2040_tiny`, `yd_rp2040` | `pico` |
| `YD_RP2040_FLASH_MB` | `4`, `16` (or set in `boards/yd_rp2040.h`) | header |
| `MECH_OE_MODE` | `DIRECT`, `FLIPFLOP` | `DIRECT` |
| `RSTBUMP` | auto-reset drives RESET directly | OFF |
| `H2O75KJMPERS` | region ident jumpers on F/H | OFF |
| `ICE_FIXED_REGION` | empty, `JAPv14orv8`, `USAv14` (F=TR only) | empty |
| `NTSCPS1YFIX75K` | NTSC PS1 Y-fix on 75k PAL | ON |
| `ICEFINALDEV1` | ICE Final dev1 patch instead of H2O | OFF |
| `ICE_DEBUG` | USB serial debug log | ON |
| `SYS_CLOCK_KHZ` | RP2040 clock; bus delays are set in ns | `240000` |
| `ICE_MECH_PATCH` | `OFF` = never drive the mechacon bus (test) | ON |

Timing constants (all in nanoseconds, converted for the clock) and the
remaining switches are in `include/config.h`.

## Debug log

Debug builds print over USB serial (any terminal, 115200). Core 1 runs the
modchip, core 0 runs USB, so the log never disturbs bus timing. It shows the
build options, every state change, each sync and patch with a read-back of
what the bus carried, and a once-a-second heartbeat while waiting.
Keys: `c` config, `p` pin levels, `m` mechacon trace, `e` bus timing,
`r` reboot.

Mechacon patches are read back 230 ns into each strobe (DIRECT builds):

```
region +08: sample df8c  A-late d8cb  want f8cb  drive ffff
```

One hex digit per strobe, bits I,H,G,B. `A-late` is what the mechacon sees;
on driven strobes it has to match `want`. The read-back is taken at the
Pico's own pins, so a series resistor or a weak drive on the console side
would not show up here.

## How it maps to the SX28

| SX28 | RP2040 |
|---|---|
| Polling loops on BIOS /OE | PIO sniffer: one sample per /OE strobe, 40 ns after it falls; strobes shorter than ~80 ns dropped |
| Patch writes on the BIOS bus | PIO writer, armed before the sync and fired the instant the last sync byte matches |
| 74HC73 + /1Q polling on mechacon A | PIO edge reader (DIRECT) or the real flip-flop (FLIPFLOP); bus read ~85 ns after A falls |
| Mechacon patch writes | PIO writer applying one half-byte per A strobe |
| MIWU wake-up pending bits | RP2040 raw GPIO edge latches, polled |
| `sleep` | polling the same wake edges |
| Interrupt at `$0000` after `SET_INTRPT` | every wait loop checks the reset / tray edges and returns to the state machine |
| Power loss in standby | core 0 restarts the Pico when BIOS /CE and /OE are both low for 1 s |
| Busy-wait timeouts | timers; the xcdvdman and PS2 logo timeouts count only BIOS-idle (CS high) time, as the asm loops do |

## Layout

```
CMakeLists.txt          build options
include/config.h        timings and switches
include/boards/         pin maps per board
pio/                    BIOS and mechacon PIO programs
src/main.c              state machine (STARTUP and the jumps between routines)
src/bios_bus.c          BIOS sniffer / writer
src/mech_bus.c          mechacon reader / writer
src/*_patch.c, ...      one file per group of asm routines
src/patch_tables.c      patch data, extracted from the asm (tools/extract_tables.py)
prebuilt/               UF2 files
```

## Credits

- Magic ICE / ICE Final and H2O-Improved authors for the original SX28
  firmware.
- [PS2-Magic-ICE-FINAL-REVERSE](https://github.com/1nick9/PS2-Magic-ICE-FINAL-REVERSE)
  for the reversed and re-sourced SX28/SX48 assembly this port is based on.
- Messiah 2 and fruitchip for reference on RP2040 / CPLD bus handling.
