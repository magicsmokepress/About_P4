// Re-entrancy guard - set true around blocking I/O (DHCP, NTP)
// to prevent lv_timer_handler() from firing during socket waits
static bool in_blocking_io = false;

static inline void safe_lv_tick() {
    if (!in_blocking_io) lv_timer_handler();
}
