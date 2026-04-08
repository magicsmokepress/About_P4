Your code → LVGL draw buffer → DSI bus → LCD controller → Panel
    │              │                │            │           │
    │         (in PSRAM)      (2-lane DSI)   (EK79007)   (IPS LCD)
    │                               │
    │                        Lane rate: e.g. 1 Gbps
    └── lv_display_flush() copies pixels from LVGL's buffer to the DSI bus
