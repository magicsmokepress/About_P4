static void preview_file(const char *path) {
    File f = SD_MMC.open(path);
    if (!f) return;

    size_t size = min((size_t)f.size(), (size_t)4096);
    char *buf = (char *)heap_caps_malloc(size + 1,
                                          MALLOC_CAP_SPIRAM);
    if (!buf) { f.close(); return; }

    f.read((uint8_t *)buf, size);
    buf[size] = 0;
    f.close();

    // Check if content is printable text
    bool is_text = true;
    for (size_t i = 0; i < min(size, (size_t)256); i++) {
        if (buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t'
            && (buf[i] < 32 || buf[i] > 126)) {
            is_text = false;
            break;
        }
    }

    if (is_text) {
        lv_label_set_text(preview_lbl, buf);
    } else {
        // Show hex dump for binary files
        char hex[1024];
        int pos = 0;
        for (size_t i = 0; i < min(size, (size_t)128) && pos < 1000; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos,
                "%02X ", (uint8_t)buf[i]);
            if ((i + 1) % 16 == 0)
                pos += snprintf(hex + pos, sizeof(hex) - pos, "\n");
        }
        lv_label_set_text(preview_lbl, hex);
    }

    free(buf);
}
