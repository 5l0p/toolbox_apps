#include <stdio.h>
#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"

/* Gallery of the newer controls: slider, progress, spin, combo, colour
 * grid, tabs, scroll window, status bar, alerts and dialogs.
 * Compile-only so far. */

#define GW 520
#define GH 420

static tb_slider *slider;
static tb_progress *progress;
static tb_spin *spin;
static tb_status *status;
static tb_tabs *tabs;
static tb_button *page1_btn;
static tb_checkbox *page2_chk;
static tb_scrollwin *scroll;

static void set_status(tb_app *app, const char *s) {
    tb_status_set(status, s);
    (void)app;
}

static void on_slider(tb_app *app, void *data) {
    (void)data;
    tb_progress_set(progress, tb_slider_get(slider));
    char buf[64];
    snprintf(buf, sizeof buf, "Slider: %d", tb_slider_get(slider));
    set_status(app, buf);
}

static void on_spin(tb_app *app, void *data) {
    (void)data;
    char buf[64];
    snprintf(buf, sizeof buf, "Spin: %d", tb_spin_get(spin));
    set_status(app, buf);
}

static void on_combo(tb_app *app, void *data) {
    (void)data;
    set_status(app, "Combo changed");
}

static void on_colour(tb_app *app, void *data) {
    (void)data;
    set_status(app, "Colour chosen");
}

static void on_tabs(tb_app *app, void *data) {
    (void)data;
    int page2 = tb_tabs_get(tabs) == 1;
    tb_widget_hide(&page1_btn->base, page2);
    tb_widget_hide(&page2_chk->base, !page2);
    set_status(app, page2 ? "Page 2" : "Page 1");
}

static void on_alert(tb_app *app, void *data) {
    (void)data;
    int r = tb_alert(app, TB_ALERT_CAUTION, "Discard the changes you made to this document?",
                     "Discard", "Cancel", NULL);
    char buf[64];
    snprintf(buf, sizeof buf, "Alert returned %d", r);
    set_status(app, buf);
}

static void on_quit(tb_app *app, void *data) {
    (void)data;
    tb_quit(app);
}

int main(void) {
    tb_app *app = tb_app_new("Gallery", GW, GH);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "Quit", on_quit, NULL);

    /* Values */
    tb_groupbox_new(app, 8, 8, 250, 124, "Values");
    slider = tb_slider_new(app, 20, 30, 220, 20, 0, 0, 100, 40, on_slider, NULL);
    slider->step = 5;
    progress = tb_progress_new(app, 20, 60, 220, 12, 100);
    tb_progress_set(progress, 40);
    tb_label_new(app, 20, 84, TB_BLACK, "Spin:");
    spin = tb_spin_new(app, 70, 80, 90, 20, 0, 10, 3, 1, on_spin, NULL);

    /* Choice */
    tb_groupbox_new(app, 270, 8, 242, 124, "Choice");
    tb_combo *combo = tb_combo_new(app, 282, 30, 200, 22, on_combo, NULL);
    tb_combo_add(combo, "Chicago");
    tb_combo_add(combo, "Geneva");
    tb_combo_add(combo, "Monaco");
    tb_combo_set(combo, "Chicago");
    tb_colorgrid_new(app, 282, 66, 200, 44, on_colour, NULL);

    /* Tabs with two pages of controls */
    tabs = tb_tabs_new(app, 8, 140, 504, 140, on_tabs, NULL);
    tb_tabs_add(tabs, "Page 1");
    tb_tabs_add(tabs, "Page 2");
    page1_btn = tb_button_new(app, 20, 180, 120, 24, "Show alert", on_alert, NULL);
    page2_chk = tb_checkbox_new(app, 20, 180, "Page 2 option", NULL, NULL);
    tb_widget_hide(&page2_chk->base, 1);

    /* Scroll window with more content than fits */
    scroll = tb_scrollwin_new(app, 8, 290, 300, 90, 600, 200);
    tb_container *prev = tb_build_begin(app, &scroll->c);
    for (int i = 0; i < 6; i++) {
        char label[32];
        snprintf(label, sizeof label, "Item %d", i + 1);
        tb_checkbox_new(app, 10, 10 + i * 30, label, NULL, NULL);
    }
    tb_button_new(app, 300, 150, 120, 24, "Far away", NULL, NULL);
    tb_build_end(app, prev);

    status = tb_status_new(app, 8, 394, 504, 18, "Ready");

    tb_run(app);
    tb_app_free(app);
    return 0;
}
