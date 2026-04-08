# Chapter 35: FFT Signal Analysis (Waveshare ESP32-P4-ETH + ESP-DSP)

## Overview
Generates composite sine wave signals (up to 2 frequencies + noise) and runs a 1024-point complex FFT using ESP-DSP. Displays the frequency-domain power spectrum on the ILI9488 display with peak detection markers.

## Hardware Required
- Waveshare ESP32-P4-ETH
- USB-C power cable
- Arduino IDE 2.x

## Libraries
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [esp-dsp](https://github.com/espressif/esp-dsp) (ESP-IDF component, included via Arduino)

## Board Settings
```
Board:      ESP32P4 Dev Module
USB Mode:   Hardware CDC and JTAG
PSRAM:      OPI PSRAM
Flash Mode: QIO 80MHz
```

## Boards Tested
- ✅ Waveshare ESP32-P4-ETH

## How to Use
1. Install `esp-dsp` component (see book Appendix for setup)
2. Open `fft_signal_analysis.ino`, upload
3. Serial commands: `f1 <Hz>` set freq 1, `f2 <Hz>` set freq 2, `noise` toggle noise
4. Display shows power spectrum with labeled peaks

## Key Concepts
- `dsps_fft2r_fc32_ae32` — ESP-DSP optimized FFT (Cooley-Tukey)
- Hann window to reduce spectral leakage
- Complex FFT output → magnitude → dB power spectrum
- Peak detection: local maximum with threshold
- LovyanGFX real-time spectrum plot with bar graph

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
