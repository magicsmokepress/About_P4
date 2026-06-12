# Chapter 21 — System Dashboard (CrowPanel Advanced 7", ESP32-P4)

The bench-verified dashboard from Chapter 21. It is **panel-only** — no external
sensors. It shows three ring gauges (CPU clock, PSRAM used, heap used), an uptime
readout, and a live touch-coordinate panel, all on the dark theme from the book.

This is the sketch the chapter is written around, confirmed running on real
CrowPanel Advanced 7" hardware.

## Files

- `dashboard.ino` — the complete sketch
- `board_config.h` — CrowPanel Advanced 7" pin/timing constants
- `lv_conf.h` — LVGL config with Montserrat **14 / 20 / 28** enabled

## Build

1. **Arduino-ESP32 core** with the **ESP32-P4** boards installed.
2. Libraries: **ESP32_Display_Panel 1.0.4** and **LVGL 9.2.x or newer**.
3. **Put `lv_conf.h` where LVGL looks for it** — next to the `lvgl` library
   folder (e.g. `Arduino/libraries/lvgl/lv_conf.h`), *not* in the sketch folder.
   Without it you get `lv_font_montserrat_20 not declared`. The copy here is the
   reference; the active one must sit beside the library.
4. Board settings: **ESP32P4 Dev Module**, PSRAM **OPI PSRAM**, Partition
   **Huge APP**.

## The one lesson to copy

The flush callback is asynchronous on MIPI-DSI. `drawBitmap()` starts a DMA
transfer and returns before the pixels land, so `lv_display_flush_ready()` is
called from the transfer-done callback via `attachDrawBitmapFinishCallback(...)`,
never right after `drawBitmap()`. Calling it synchronously is what causes the
flicker / washed-out colors / soft text that wrecks a first dashboard.

## Swapping in real sensors

Replace the body of `update_metrics()` with reads from any earlier chapter (e.g.
the BME280 from Chapter 14) and the same rings become a weather station — the
display, touch, and flush code are unchanged.
