#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"

/* TextEdit: a small editor built on the toolbox. Tracks unsaved changes,
 * asks before discarding them, and keeps the window title and status bar
 * up to date. */

#define ED_W 520
#define ED_H 340
#define ED_STATUS_H 18
#define ED_MENU_H 20

static tb_textview *editor;
static tb_status *status;
static char filename[TB_PATH_MAX] = "untitled.txt";
static int named = 0;              /* 1 once the document has a real file */

/* ---- paths -------------------------------------------------------------- */

/* Directory part of a path ("." if none). */
static void dir_of(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) snprintf(out, cap, ".");
    else if (slash == path) snprintf(out, cap, "/");
    else snprintf(out, cap, "%.*s", (int)(slash - path), path);
}

static const char *base_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ---- title and status ---------------------------------------------------- */

static void refresh(tb_app *app) {
    int line, col;
    char title[TB_PATH_MAX + 32], buf[128];
    int edited = tb_textview_modified(editor);

    snprintf(title, sizeof title, "%s%s - TextEdit",
             base_of(filename), edited ? " (edited)" : "");
    tb_set_title(app, title);

    tb_textview_cursor(editor, &line, &col);
    snprintf(buf, sizeof buf, "Line %d, Col %d      %s",
             line, col, edited ? "Edited" : "Saved");
    tb_status_set(status, buf);
}

static void on_update(tb_app *app, void *data) {
    (void)data;
    refresh(app);
}

/* ---- loading and saving -------------------------------------------------- */

static int load_file(tb_app *app, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        tb_alert(app, TB_ALERT_STOP, "Could not open that file.", "OK", NULL, NULL);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    tb_textview_set(editor, buf);
    free(buf);
    return 1;
}

static int write_file(tb_app *app) {
    char *s = tb_textview_get(editor);
    FILE *f = fopen(filename, "wb");
    int ok = 0;
    if (f) {
        ok = fputs(s, f) >= 0;
        if (fclose(f) != 0) ok = 0;
    }
    free(s);
    if (ok) tb_textview_clear_modified(editor);
    else tb_alert(app, TB_ALERT_STOP, "Could not save the file.", "OK", NULL, NULL);
    return ok;
}

/* Save As: ask for a name. Returns 1 if the document was saved. */
static int save_as(tb_app *app) {
    char dir[TB_PATH_MAX], path[TB_PATH_MAX];
    dir_of(filename, dir, sizeof dir);
    if (!tb_save_dialog(app, dir, base_of(filename), path, sizeof path))
        return 0;
    snprintf(filename, sizeof filename, "%s", path);
    named = 1;
    return write_file(app);
}

/* Save to the current file, or ask for a name if the document has none. */
static int save_document(tb_app *app) {
    return named ? write_file(app) : save_as(app);
}

/* Before throwing the text away: 1 = go ahead, 0 = cancel. */
static int confirm_discard(tb_app *app) {
    if (!tb_textview_modified(editor)) return 1;
    char msg[TB_PATH_MAX + 64];
    snprintf(msg, sizeof msg, "Save changes to \"%s\" before continuing?", base_of(filename));
    int r = tb_alert(app, TB_ALERT_CAUTION, msg, "Save", "Discard", "Cancel");
    if (r == 1) return save_document(app);
    if (r == 2) return 1;
    return 0;
}

/* ---- File menu --------------------------------------------------------- */

static void file_new(tb_app *app, void *data) {
    (void)data;
    if (!confirm_discard(app)) return;
    tb_textview_set(editor, "");
    snprintf(filename, sizeof filename, "untitled.txt");
    named = 0;
    refresh(app);
}

static void file_open(tb_app *app, void *data) {
    char dir[TB_PATH_MAX], path[TB_PATH_MAX];
    (void)data;
    if (!confirm_discard(app)) return;
    dir_of(filename, dir, sizeof dir);
    if (tb_open_dialog(app, dir, path, sizeof path) && load_file(app, path)) {
        snprintf(filename, sizeof filename, "%s", path);
        named = 1;
    }
    refresh(app);
}

static void file_save(tb_app *app, void *data) {
    (void)data;
    save_document(app);
    refresh(app);
}

static void file_save_as(tb_app *app, void *data) {
    (void)data;
    save_as(app);
    refresh(app);
}

static void file_quit(tb_app *app, void *data) {
    (void)data;
    if (confirm_discard(app)) tb_quit(app);
}

/* Window close button: same check as Quit. */
static int on_close(tb_app *app, void *data) {
    (void)data;
    return confirm_discard(app);
}

/* ---- Edit menu --------------------------------------------------------- */

static void edit_cut(tb_app *app, void *data) {
    (void)data;
    tb_textview_cut(app, editor);
}

static void edit_copy(tb_app *app, void *data) {
    (void)data;
    tb_textview_copy(app, editor);
}

static void edit_paste(tb_app *app, void *data) {
    (void)data;
    tb_clip_paste(app);
}

static void edit_select_all(tb_app *app, void *data) {
    (void)data;
    tb_textview_select_all(app, editor);
}

/* ---- main -------------------------------------------------------------- */

/* Text fills the window below the menubar; the status bar sits at the bottom. */
static void layout(tb_app *app) {
    tb_textview_resize(app, editor, 0, ED_MENU_H,
                       app->width, app->height - ED_MENU_H - ED_STATUS_H);
    status->base.y = app->height - ED_STATUS_H;
    status->base.w = app->width;
    tb_damage_all(app);
}

static void on_resize(tb_app *app, void *data) {
    (void)data;
    layout(app);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        snprintf(filename, sizeof filename, "%s", argv[1]);
        named = 1;
    }

    tb_app *app = tb_app_new("TextEdit", ED_W, ED_H);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "New", file_new, NULL);
    tb_menu_item(app, "Open...", file_open, NULL);
    tb_menu_item(app, "Save", file_save, NULL);
    tb_menu_item(app, "Save As...", file_save_as, NULL);
    tb_menu_item(app, "-", NULL, NULL);
    tb_menu_item(app, "Quit", file_quit, NULL);

    tb_menu_new(app, "Edit");
    tb_menu_item(app, "Cut", edit_cut, NULL);
    tb_menu_item(app, "Copy", edit_copy, NULL);
    tb_menu_item(app, "Paste", edit_paste, NULL);
    tb_menu_item(app, "-", NULL, NULL);
    tb_menu_item(app, "Select All", edit_select_all, NULL);

    /* the text fills the window below the menubar; the status bar sits at
     * the bottom */
    editor = tb_textview_new(app, 0, ED_MENU_H, ED_W, ED_H - ED_MENU_H - ED_STATUS_H);
    status = tb_status_new(app, 0, ED_H - ED_STATUS_H, ED_W, ED_STATUS_H, "");
    tb_textview_on_update(editor, on_update, NULL);
    tb_on_close(app, on_close, NULL);
    tb_on_resize(app, on_resize, NULL);

    FILE *probe = fopen(filename, "rb");   /* a missing file is an empty document */
    if (probe) {
        fclose(probe);
        load_file(app, filename);
    }
    refresh(app);

    tb_run(app);
    tb_app_free(app);
    return 0;
}
