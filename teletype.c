#include <signal.h>
#include <stdlib.h>
#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"
#include "teletype.h"

/* Teletype: the standalone dumb terminal. The panel itself lives in
 * teletype.h so codeedit can host it as a tab. */

#define TT_W 520
#define TT_H 320

static tt_panel *panel;

static void quit_app(tb_app *app, void *data) {
    (void)data;
    tb_quit(app);
}

static void layout(tb_app *app) {
    tt_panel_place(app, panel, 0, 0, app->width, app->height);
}

static void on_resize(tb_app *app, void *data) {
    (void)data;
    layout(app);
}

int main(void) {
    /* A write to a dead shell must fail with EPIPE, not kill us. */
    signal(SIGPIPE, SIG_IGN);

    tb_app *app = tb_app_new("Teletype", TT_W, TT_H);
    if (!app) return 1;

    panel = tt_panel_new(app);
    panel->on_exit = quit_app;            /* the app ends with its shell */
    app->root.focus = &panel->input->base;

    layout(app);
    tb_on_resize(app, on_resize, NULL);
    tt_panel_start(app, panel);

    tb_run(app);
    tt_panel_stop(panel);
    tb_app_free(app);
    return 0;
}
