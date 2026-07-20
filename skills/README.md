# Skills

This folder contains **Claude Code skills** for ESP32-P4 development.

## What Are Skills?

Skills are markdown instruction files that Claude Code loads automatically when it detects relevant code in your project. They encode hard-won knowledge — pin mappings, configuration gotchas, board-specific workarounds — so you don't have to re-discover them.

## Available Skills

| Skill | Description |
|-------|-------------|
| [`esp32-p4-arduino/`](esp32-p4-arduino/SKILL.md) | ESP32-P4 Arduino development — 50+ gotchas, pin mappings for 3 boards, golden config |

## Installation

1. Install [Claude Code](https://docs.anthropic.com/en/docs/claude-code)
2. Clone this repo
3. Open your ESP32-P4 project in Claude Code

The skill activates automatically when Claude Code detects P4 Arduino code. No manual configuration required.

## What's Inside

The ESP32-P4 Arduino skill encodes:

- **Pin mappings** for the Elecrow CrowPanel Advanced 7", M5Stack Tab5, and Waveshare ESP32-P4-ETH
- **The golden board settings** that actually work (OPI PSRAM, partition scheme, flash settings)
- **50+ gotchas** discovered through months of development
- **Board-specific workarounds** for display, touch, USB, camera, and more
- **Anti-patterns** that waste hours if you don't know about them

## Documentation

See **Chapter 36** of *Programming the ESP32-P4* for full documentation on AI-assisted ESP32-P4 development and how this skill was built.
