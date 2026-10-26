# Field Recorder firmware

Firmware for the **Waveshare ESP32-S3-ePaper-3.97**: the device UI from `Field Recorder v2` and the web interface from `Field Recorder Web`, running on the board.

## Flash it

1. Install [PlatformIO](https://platformio.org) (VS Code extension or CLI).
2. Plug the board in over USB-C. If it isn't detected: hold **BOOT**, tap **RESET**, release BOOT.
3. From this folder:
   ```
   pio run -t upload
   pio device monitor
   ```
   The serial log prints what mounted (SD, PMIC, RTC) and every e-paper refresh.

## Updates

The app area is split into two 2 MB slots. An update is written into the slot that is not running, and the device restarts into it. If the new firmware crashes or hangs before it has run for 30 seconds, the bootloader goes back to the previous one by itself.

- **From GitHub (any network):** the device checks the latest release of `UPDATE_REPO` (see `src/config.h`) about 20 s after going online and every 6 hours. When there is a newer version, the home screen header shows `UPDATE x.y.z` and *Settings → Firmware* offers to install it. The web interface has the same under *Device → Firmware*.
- **From a file:** *Device → Firmware → Install from a file* in the web interface takes a `firmware.bin`.

To publish a version: push your changes, then on GitHub open *Actions → Firmware → Run workflow*, fill in the version (for example `1.2.0`, higher than the last one) and a short note. The workflow builds it with that version number and makes release `v1.2.0` with `firmware.bin` and `manifest.json`; the note is shown on the device. Pushing a tag `v1.2.0` does the same, with the tag message as the note. The repository must stay public so devices can download without a token.

Switching an older device (one app slot) to this layout needs one USB flash of bootloader, partition table and app, with `otadata` erased: `pio run -t upload` does that, or flash `bootloader.bin` at 0x0, `partitions.bin` at 0x8000 and `firmware.bin` at 0x10000 and erase 0xe000-0xffff. Settings (NVS) and the internal FAT stay where they are.

## Power

- Sleeps after the time set under *Settings → Sleep after* (not while recording, sending or updating). The screen then shows **ASLEEP**; any rocker direction wakes it.
- **Hold PWR for 1.5 s** to switch off: the recording is saved, the screen shows **OFF**, and the power chip cuts the power. Press PWR to switch on. Holding PWR for 10 s forces the power off.
- On an empty battery the screen shows **EMPTY** and the device shuts down cleanly.

## Check before first run

- **SD card pins.** `src/config.h` → `PIN_SD_*` are best guesses. Copy the real values from Waveshare's `05_SD_Test` Arduino example for this board. Until they're right, recordings go to the internal flash (about 11 MB, about 6 minutes of audio).
- **Orientation.** If the screen is upside down, set `EPD_ROTATION` to `3`.
- **Rocker direction.** If ▲/▼ are reversed, set `BTN_SWAP_UPDOWN 1`.
- **Mic level.** Too quiet or clipping: change `MIC_PGA`, `MIC_DGAIN` or `REC_GAIN`.

## Fonts (optional, recommended)

The build works out of the box with Adafruit FreeFonts. To get Literata and Geist Mono from the design:

```
pip install pillow
# put Literata-Regular.ttf and GeistMono-Regular.ttf in tools/ttf/
python tools/make_fonts.py
pio run -t upload
```

## What's in it

- **Controls:** ▲▼ navigate, push confirms, hold push (or BOOT) goes back. ▲▼ auto-repeat when held.
- **E-paper:** each frame is drawn into a buffer, diffed against the last one, and only the changed bands are partially refreshed. A full refresh runs after `fullEvery` partials (default 30), on the next screen change.
- **Recording:** 16 kHz mono WAV in `/rec/` on the card, pause, marks, flushed every 30 s. A recording cut off by power loss is repaired on boot.
- **Playback:** built-in speaker, ±10 s.
- **Wi-Fi:** multiple saved networks joined in priority order, scan, password from your phone (QR → device hotspot → page) or the on-device letter wheel. 2.4 GHz only.
- **Whisper:** OpenAI (`whisper-1`, summary and action items via `gpt-4o-mini`) or a self-hosted OpenAI-compatible server (for example faster-whisper-server/speaches). Long recordings are sent in 10-minute parts to stay under the 25 MB API limit.
- **Local network:** the device's own access point at `http://192.168.4.1` (also `http://field.local` on your home network). Phones that join get a captive-portal popup.
- **Web interface:** recordings (stream, download WAV and TXT, rename, delete, send), transcript and summary, remote recording, Wi-Fi, local network, Whisper and device settings.

## Files

| File | Role |
|---|---|
| `src/config.h` | Pins and defaults |
| `src/ui.cpp` | Device screens and rocker logic |
| `src/display.cpp` | Framebuffer, diff-based partial refresh, text |
| `src/audio.cpp` | ES8311 codec, recording, playback |
| `src/net.cpp` | Wi-Fi station and access point, scanning, DNS, NTP |
| `src/jobs.cpp` | Whisper upload, summary, Wi-Fi join, Whisper test |
| `src/web.cpp`, `src/web_ui.h` | HTTP API and the web interface |
| `src/storage.cpp`, `src/settings.cpp`, `src/board.cpp` | SD/flash, saved settings, PMIC, RTC |

## Known limits

- TLS uses `setInsecure()` (no certificate check). Pin a root CA before shipping.
- The access point shares the radio channel with your home Wi-Fi, so phones on the access point briefly drop when the device changes networks.
- Whisper does not identify speakers. Transcript paragraphs are labelled with their timestamps.
