#pragma once
// ---- Field Recorder · board + behaviour configuration ----
#ifndef FW_VERSION
#define FW_VERSION "1.4.0"   // release builds pass the git tag instead (-DFW_VERSION=...)
#endif

// Updates: GitHub releases of this (public) repo. Each release carries firmware.bin and manifest.json.
#define UPDATE_REPO     "paulbokel/field-recorder"
#define UPDATE_EVERY_H  6     // check this often while online
#define MDNS_NAME  "field"                       // http://field.local
#define TZ_INFO    "CET-1CEST,M3.5.0,M10.5.0/3"  // POSIX TZ string
#define NTP_SERVER "pool.ntp.org"

// e-paper 3.97" 800x480, SSD1677 (GxEPD2_397_GDEM0397T81)
#define PIN_EPD_SCK  11
#define PIN_EPD_MOSI 12
#define PIN_EPD_CS   10
#define PIN_EPD_DC   9
#define PIN_EPD_RST  46
#define PIN_EPD_BUSY 3
#define EPD_SPI_HZ   10000000
#define EPD_ROTATION 3        // 1 or 3 – flip if the UI appears upside down

// Rotary rocker (active LOW) + BOOT as extra "back"
#define PIN_BTN_UP   4
#define PIN_BTN_PUSH 5
#define PIN_BTN_DOWN 6
#define PIN_BTN_BOOT 0
#define BTN_SWAP_UPDOWN 0     // set 1 if ▲/▼ feel reversed
#define HOLD_MS 600

// Power
#define AP_IDLE_MS   (5UL * 60000)   // own network switches off after this long without use (unless someone is on it)
#define SLEEP_MIN_DEFAULT 10          // deep sleep after this many idle minutes (0 = never)

// I2C bus: ES8311, PCF85063 RTC, SHTC3, AXP2101-class PMIC (TG28)
#define PIN_I2C_SDA 41
#define PIN_I2C_SCL 42
#define ADDR_PMIC   0x34
#define ADDR_RTC    0x51
#define ADDR_ES8311 0x18

// Audio: ES8311 codec + NS4150B amplifier
#define PIN_I2S_MCLK 13
#define PIN_I2S_BCLK 14
#define PIN_I2S_WS   47
#define PIN_I2S_DOUT 48   // ESP -> codec (speaker)
#define PIN_I2S_DIN  21   // codec -> ESP (microphone)
#define PIN_PA_EN    39   // HIGH = amplifier on
#define SAMPLE_RATE  16000
#define MIC_DGAIN    ES8311_MIC_GAIN_24DB // ES8311 reg 0x16: ADC gain 0..7 x 6 dB
#define REC_GAIN     1    // extra software gain

// microSD (SDMMC) – pins from Waveshare's 05_SD_Test / sdcard_bsp for this board.
// If the card does not mount, recordings go to the 11 MB internal flash instead.
#define SD_4BIT     0
#define PIN_SD_CLK  16
#define PIN_SD_CMD  17
#define PIN_SD_D0   15
#define PIN_SD_D1   7
#define PIN_SD_D2   8
#define PIN_SD_D3   18

// Whisper
#define OPENAI_BASE          "https://api.openai.com"
#define OPENAI_STT_MODEL     "whisper-1"
#define OPENAI_SUMMARY_MODEL "gpt-4o-mini"
#define CHUNK_SECONDS        600   // 16 kHz WAV: 10 min ≈ 19 MB, under the 25 MB API limit

// Mistral Voxtral (EU): transcription with speaker labels
#define MISTRAL_BASE         "https://api.mistral.ai"
#define MISTRAL_STT_MODEL    "voxtral-mini-latest"
#define MISTRAL_CHUNK_SECONDS 3600 // Mistral takes up to 60 min (500 MB) per request; speakers stay consistent within one part
