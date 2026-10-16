#include <stdio.h>
#include <string.h>
#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"

/* Exercises the core controls: checkbox, radio group, pop-up button,
 * single-line field, group box, rule and static icon. */

#define CW 520
#define CH 400

static tb_label *status;
static tb_field *name_field;

static void report(tb_app *app, void *data) {
    tb_label_set(app, status, (const char *)data);
}

static void name_accept(tb_app *app, void *data) {
    char buf[128];
    (void)data;
    snprintf(buf, sizeof buf, "Name accepted: %s", tb_field_get(name_field));
    tb_label_set(app, status, buf);
}

static void font_changed(tb_app *app, void *data) {
    (void)data;
    tb_label_set(app, status, "Font changed");
}

static void quit_cb(tb_app *app, void *data) {
    (void)data;
    tb_quit(app);
}

/* 16x16 1-bit frame icon: 2 bytes per row, MSB first. */
static const unsigned char frame_icon[32] = {
    0xFF, 0xFF,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0xFF, 0xFF
};

int main(void) {
    tb_app *app = tb_app_new("Controls", CW, CH);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "Quit", quit_cb, NULL);

    /* Options group: checkboxes and a radio group */
    tb_groupbox_new(app, 8, 8, 250, 132, "Options");
    tb_checkbox_new(app, 20, 30, "Show hidden files", report, "Hidden files toggled");
    tb_checkbox_new(app, 20, 50, "Word wrap", report, "Word wrap toggled");

    tb_radio *small = tb_radio_new(app, NULL, 20, 76, "Small", report, "Size: small");
    (void)small;
    tb_radio_new(app, small, 20, 94, "Medium", report, "Size: medium");
    tb_radio_new(app, small, 20, 112, "Large", report, "Size: large");

    /* Font group: pop-up button */
    tb_groupbox_new(app, 270, 8, 242, 132, "Font");
    tb_popup *font = tb_popup_new(app, 282, 30, 200, 20, font_changed, NULL);
    tb_popup_add(font, "Chicago");
    tb_popup_add(font, "Geneva");
    tb_popup_add(font, "Monaco");
    tb_popup_add(font, "New York");

    tb_rule_new(app, 8, 152, 504, 1);

    /* Name line: label, single-line field, icon */
    tb_label_new(app, 8, 170, TB_BLACK, "Name:");
    name_field = tb_field_new(app, 60, 166, 300, 22);
    tb_field_set(name_field, "Untitled");
    tb_field_on_accept(name_field, name_accept, NULL);
    tb_icon_new(app, 440, 166, 16, 16, frame_icon, TB_NAVY);

    tb_button_new(app, 340, 200, 80, 24, "OK", report, "OK pressed");
    tb_button_new(app, 430, 200, 80, 24, "Cancel", report, "Cancel pressed");

    status = tb_label_new(app, 8, 380, TB_NAVY, "Tab moves focus, Return accepts the name");

    tb_run(app);
    tb_app_free(app);
    return 0;
}
