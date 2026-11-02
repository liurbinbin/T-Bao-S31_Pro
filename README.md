# T-Bao S31 VoCat

AI pet firmware for the **T-Bao S31 / ESP32-S31**. It combines Xiaozhi voice chat, a round touch UI, camera vision, media playback, and motor control in one ESP-IDF application.

> Status: the T-Bao S31 board definition, camera/audio/display drivers, and application features are integrated and buildable. Camera PID, PMIC, LCD, touch, audio, and SD-card behavior still require a hardware smoke test.

[中文说明](README_cn.md)

## Features

- **Xiaozhi voice chat**: offline wake words ("你好喵伴" and "你好小智") with WebSocket cloud ASR, LLM, and TTS, plus emote states and live captions.
- **Round touch UI**: LVGL 9 and `esp_emote_expression` with launcher, dialogue, camera, music, video, game, timer, motion, settings, and about pages.
- **Camera and vision**: OV2640/OV3660-compatible PARLIO JPEG camera with on-screen preview, HTTP MJPEG `/stream`, JPEG `/snapshot`, recording, lightweight face detection/face following, and guard mode.
- **Media playback**: local audio/video from microSD; online music search resolves a playable URL from multiple public APIs.
- **Motion control**: dual motors and a servo with touch, Web, voice/MCP control sources, speed limits, motion sequences, and failsafe handling.
- **Connectivity and control**: Wi-Fi provisioning, volume/brightness settings, MCP tools, and an HTTP preview service.

## Hardware

The supported board name is `tbao_s31_board`; its definition is in [`boards/tbao_s31_board`](boards/tbao_s31_board).

| Module | Configuration |
| --- | --- |
| MCU | ESP32-S31, target `esp32s31`, 16 MB Flash + PSRAM |
| LCD | ST7796S, approximately 1.54 inch, 320x320, 8-bit PARLIO/I80; data GPIO36/37/38/39/40/42/43/44 |
| Touch | FT6336/FT6336U at I2C address `0x38`; reset and interrupt are provided by XL9555 |
| Audio | ES8389 playback + ES7210 dual-microphone capture; I2S GPIO6/7/8/9/10 |
| Camera | OV2640/OV3660, PARLIO JPEG; SCCB GPIO3/4, RESET GPIO45, XCLK GPIO55, data GPIO46..53, PCLK GPIO54, HREF GPIO57 |
| Storage | microSD over 4-bit SDMMC: D0..D3 = GPIO20..23, CLK = GPIO24, CMD = GPIO25, mounted at `/sdcard` |
| Expansion/actuators | XL9555 GPIO expander, dual motor driver, servo output, and LCD backlight PWM |

See [`pinmap_cn.md`](pinmap_cn.md) for the complete pin, power, and connector mapping.

## Requirements

- ESP-IDF **6.1 or newer**, configured for target `esp32s31`. The repository scripts are written around a 6.2 toolchain environment.
- Python, CMake, Ninja, and the `riscv32-esp-elf` toolchain supplied by ESP-IDF.

Check the environment:

```bash
idf.py --version
idf.py --list-targets | findstr esp32s31
```

## Build and flash

Run these commands from the repository root:

```bash
# Select the target on a fresh checkout
idf.py set-target esp32s31

# Resolve the remaining registry dependencies
idf.py reconfigure

# Board Manager sources for T-Bao S31 are checked into components/gen_bmgr_codes
# No generation step is needed on a fresh checkout.

# Build
idf.py build

# Flash and monitor (replace COM12 with the actual port)
idf.py -p COM12 -b 921600 flash
idf.py -p COM12 monitor
```

The ESP-DL, face detector, patched runtime components, and generated T-Bao S31 Board Manager component are checked into `components/`; they no longer depend on modified files under `managed_components/`. After changing files under `boards/tbao_s31_board`, run `idf.py gen-bmgr-config -b tbao_s31_board -c ./boards` again, then reconfigure and build.

[`build_only.ps1`](build_only.ps1) and [`esptool_flash.ps1`](esptool_flash.ps1) are included for the original local Windows setup. They contain machine-specific ESP-IDF paths, Python paths, and `COM12`; review those values before using them on another machine. The `idf.py` flow above is the portable option.

## SD-card layout

The application mounts the card at `/sdcard`:

| Content | Location |
| --- | --- |
| Music | Playable audio files in `/sdcard` |
| Video | MP4/AVI/MJPEG files in `/sdcard` |
| Recordings | `/sdcard/record` |

## HTTP preview

After the device joins Wi-Fi, open the device IP at:

- `/` for the preview and device controls
- `/snapshot` for the current JPEG frame
- `/stream` for an MJPEG stream (one long-lived viewer)

Use the IP address printed in the serial log.

## Project layout

```text
main/                         application, pages, camera, audio, motion, and HTTP services
boards/tbao_s31_board/       T-Bao S31 Board Manager definition
components/                   local components, board adapters, and vendored runtime dependencies
managed_components/           remaining ESP-IDF Component Manager dependencies
patches/                      source patches retained for reference and any remaining managed dependencies
assets/                       boot splash and UI assets
docs/                         design and porting notes
tests/                        Python contract tests
```

## Tests and validation

Run the repository's static contract tests:

```bash
python -m unittest discover -s tests -p "test_*.py"
```

A successful build only confirms that the firmware and generated assets can be produced. On first hardware bring-up, also verify camera detection and power rails, LCD/touch orientation, ES8389 playback, ES7210 capture, Wi-Fi provisioning, SD-card I/O, and motor/servo safe stop behavior.

## License

Application code is Apache-2.0 unless a file states otherwise. Components and model/asset packages retain their upstream licenses; see the corresponding component directories for details.
