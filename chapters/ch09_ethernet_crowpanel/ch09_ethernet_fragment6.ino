static void ui_log(const char *msg) {
  Serial.println(msg);
  int cur = strlen(log_buf);
  int add = strlen(msg);
  if (cur + add + 2 > (int)sizeof(log_buf) - 1) {
    int shift = cur + add + 2 - (int)sizeof(log_buf) + 1;
    if (shift < cur) memmove(log_buf, log_buf + shift, cur - shift + 1);
    else log_buf[0] = '\0';
  }
  if (log_buf[0] != '\0') strcat(log_buf, "\n");
  strcat(log_buf, msg);
  if (log_lbl) lv_label_set_text(log_lbl, log_buf);
}
