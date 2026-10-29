Prebuilt UF2 firmware - hold BOOTSEL, plug in USB, copy the file.

All files are the F=TR build:
  RSTBUMP=OFF  H2O75KJMPERS=OFF  NTSCPS1YFIX75K=ON  H2O dev1
  DIRECT mechacon OE (A straight to a GPIO), except *_ff (74HC73 flip-flop)

Only the F=TR build on the Waveshare RP2040-Tiny (tiny_*) has been tested
and confirmed working. The others are built from the same code but untested.

  ./        region from the BIOS (normal)
  v8jap/    JAPv14orv8: fixed JAP 75k region; v7/v8 JAP consoles take the
            v9+ mechacon patch
  usa/      USAv14: fixed USA 75k region

  tiny_dbg / tiny_nodbg   Waveshare RP2040-Tiny, USB debug log on / off
  pico_dbg / pico_nodbg   Raspberry Pi Pico, debug on / off
  pico_ff                 Raspberry Pi Pico, flip-flop wiring, debug on
  yd16_dbg / yd16_nodbg   YD-RP2040 16 MB, debug on / off
  yd4_dbg                 YD-RP2040 4 MB, debug on

The debug banner "features :" line shows the build options and region.
