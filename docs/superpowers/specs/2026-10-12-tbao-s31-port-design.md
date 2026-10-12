# T-Bao S31 Port Design

## Scope

Port the existing VoCat application to the T-Bao S31 hardware described by
`T-Bao-S31/docs/pinmap_cn.md`. The target remains the existing Xiaozhi chat
application and keeps its HTTP snapshot/stream, MCP photo, SD MJPEG recorder,
camera page, guard mode, audio, LCD, and touch workflows.

## Hardware Contract

- Target: `esp32s31`, with an IDF 6.1 build environment.
- Main I2C bus: `I2C_NUM_0`, SDA GPIO0, SCL GPIO1.
- XL9555: 7-bit address `0x22`, interrupt GPIO2. P00 is touch INT, P01 is
  touch RESET, P05 enables the 5 V/audio rail, P07 controls the amplifier,
  and P10 enables SD power.
- Camera SCCB: independent `I2C_NUM_1`, SDA GPIO3, SCL GPIO4. Sensor reset is
  GPIO45 and XCLK is GPIO55.
- Camera data is the non-standard physical mapping D2..D9 to PARLIO data
  indexes GPIO46..GPIO53. PCLK is GPIO54 and HREF is GPIO57. VSYNC GPIO56 is
  documented but the local PARLIO camera component does not implement VSYNC;
  configure it as NC and use HREF/DE capture.
- Camera PWDN is hard-wired low. The sensor must not claim a PWDN GPIO.
- Camera sensors are OV2640 at 7-bit SCCB address `0x30` and OV3660 at
  `0x3C`. The selected sensor is identified by PID before streaming.
- Camera PMIC SGM38121 is on the main I2C bus at 7-bit address `0x28`.
  AVDD/DOVDD use register value `0xB1`; OV2640 DVDD uses `0x57` (or the
  explicitly selected 1.3 V value `0x64`), while OV3660 uses `0x7D`.
  A sensor/profile mismatch is a startup error, not a silent fallback.
- LCD is ST7796S, 320x320, 8-bit I80/PARLIO: RS18, WR17, CS19, RST35,
  data D0..D7 GPIO44,43,42,40,39,38,37,36, and backlight PWM GPIO16.
- FT6336/FT6336U is 7-bit address `0x38`; its INT and RESET are XL9555 P00
  and P01, so the touch driver receives `GPIO_NUM_NC` for both native pins.
- Audio uses ES8389 at 7-bit `0x10` / Board Manager address `0x20`, ES7210 at
  7-bit `0x40` / Board Manager address `0x80`, and I2S GPIO6/7/8/9/10 for
  MCLK/DSDIN/BCLK/LRCK/ASDOUT. ES7210 is a two-real-microphone `MM` layout.
- SDIO uses D0..D3 GPIO20..23, CLK GPIO24, CMD GPIO25 and XL9555 P10 for
  the SD power enable.

## Architecture

Create a separate `boards/tbao_s31_board` definition and select it through
Board Manager. Keep `esp_vocat_s31_board` unchanged. Add the local PARLIO
camera component and the local ST7796S panel component to the target so that
the target can build reproducibly without changing managed component sources.

Replace the old SC101IOT V4L2 `/dev/video2` plus `/dev/video10` pipeline with
one PARLIO camera task. It emits JPEG frames directly, stores a latest-frame
copy, and exposes the existing callback/capture API. Consumers continue to use
that API, while the on-screen preview and guard analyzer decode JPEG to RGB565
with the S31 hardware JPEG decoder.

Board Manager owns the primary I2C bus and LCD/touch/audio/XL9555 devices.
The camera module obtains the existing primary I2C handle for SGM38121 and
creates only its independent SCCB bus on `I2C_NUM_1`.

## Lifecycle and Failure Handling

- Camera start is reference-counted through the existing activity manager.
- Camera start sequence is PMIC power configuration, sensor reset/XCLK/SCCB
  setup, PID probe, sensor configuration, PARLIO allocation, and stream start.
- Any PMIC readback, PID, sensor profile, PARLIO, or buffer allocation failure
  stops the camera and releases all resources. The application remains usable
  without camera features.
- Frame callbacks are copied under the callback contract. Unregistering one
  consumer never clears the other consumers.
- LCD and touch are initialized by Board Manager. Touch reset is asserted via
  XL9555 P01 before creating the FT6336 driver; P00 remains an input for
  diagnostics and future interrupt integration.

## Verification

- Generate Board Manager code with IDF 6.1 and inspect the generated board
  selection and PARLIO/LCD/touch/audio configuration.
- Reconfigure and build the target with no SC101IOT, V4L2, or `esp_video`
  references in the application sources.
- Verify the build selects the S31 hardware JPEG decoder and the local OV2640 /
  OV3660 sensor sources.
- On hardware, verify logs for SGM38121 readback, OV2640/OV3660 PID, PARLIO
  capture, JPEG frame dimensions, ST7796S initialization, FT6336 touch, audio,
  and SD power/mount.
