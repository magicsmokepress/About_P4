pwd_ta = lv_textarea_create(pwd_panel);
lv_textarea_set_password_mode(pwd_ta, true);
lv_textarea_set_one_line(pwd_ta, true);

kb = lv_keyboard_create(pwd_panel);
lv_keyboard_set_textarea(kb, pwd_ta);
