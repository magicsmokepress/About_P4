lv_obj_t *tabview = lv_tabview_create(scr);
lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
lv_tabview_set_tab_bar_size(tabview, 50);

lv_obj_t *tab_env = lv_tabview_add_tab(tabview, "Environment");
lv_obj_t *tab_charts = lv_tabview_add_tab(tabview, "Charts");
lv_obj_t *tab_net = lv_tabview_add_tab(tabview, "Network");

// Style the tab bar
lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x16213E), 0);
lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_16, 0);
