# Skills

This folder contains **Claude Code skills** for ESP32-P4 development.

## What Are Skills?

Skills are markdown instruction files that Claude Code loads automatically when it detects relevant code in your project. They encode hard-won knowledge — pin mappings, configuration gotchas, board-specific workarounds — so you don't have to re-discover them.

## Available Skills

| File | Description |
|------|-------------|
| `esp32-p4-arduino.md` | ESP32-P4 Arduino development — 50+ gotchas, pin mappings for 3 boards, golden config |

## Installation

1. Install [Claude Code](https://docs.anthropic.com/en/docs/claude-code)
2. Clone this repo
3. Open your ESP32-P4 project in Claude Code

The skill activates automatically when Claude Code detects P4 Arduino code. No manual configuration required.

## What's Inside

The ESP32-P4 Arduino skill encodes:

- **Pin mappings** for CrowPanel 7.0", Waveshare ESP32-P4-NANO, and M5Stack M5P4NANO
- **The golden `sdkconfig`** that actually works (SPI RAM timing, PSRAM config, flash settings)
- **50+ gotchas** discovered through months of development
- **Board-specific workarounds** for display, touch, USB, camera, and more
- **Anti-patterns** that waste hours if you don't know about them

## Documentation

See **Chapter 36** of *ESP32-P4: The Definitive Guide* for full documentation on AI-assisted ESP32-P4 development and how this skill was built.
