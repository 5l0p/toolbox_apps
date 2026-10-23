#define _XOPEN_SOURCE 700

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "codeconfig.h"
#include "deps/toolbox.h/toolbox.h"
#include "teletype.h"

/* CodeEdit: a ProjectCenter style editor. A file browser (Miller columns)
 * fills the top pane; a tabbed pane below, split by a draggable bar, holds
 * the open files and a fixed Teletype tab (a shell). Double-click a file in
 * the browser to open it. */

#define CE_W 720
#define CE_H 480
#define CE_STATUS_H 18
#define CE_MIN_BROWSER 60     /* smallest browser pane height     */
#define CE_MIN_EDITOR 120     /* smallest editor pane height      */
#define CE_COL_W 170          /* browser column width             */

/* One tab. A file or untitled document has a text view; the Teletype tab has
 * a terminal panel instead. */
typedef struct ce_doc {
    char *path;               /* NULL = untitled or Teletype     */
    int no;                   /* number for untitled documents   */
    char label[TB_PATH_MAX + 16];   /* text shown on the tab    */
    tb_textview *tv;          /* NULL for the Teletype tab       */
    tt_panel *term;           /* NULL for documents              */
} ce_doc;

static tb_miller *browser;
static tb_split *split;
static tb_tabs *tabs;
static tb_status *status;
static ce_doc **docs;
static int ndocs, cdocs;
static tb_textview **spare;       /* hidden text views, reused by new docs */
static int nspare, cspare;
static int untitled_count;
static char root[TB_PATH_MAX];   /* folder shown in the browser        */

/* ---- paths -------------------------------------------------------------- */

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

/* Full path of a browser node: root plus the names of its ancestors. */
static void node_path(const tb_node *n, char *out, size_t cap) {
    const tb_node *chain[64];
    int k = 0;
    for (const tb_node *p = n; p && p->depth >= 0 && k < 64; p = p->parent)
        chain[k++] = p;
    snprintf(out, cap, "%s", root);
    for (int i = k - 1; i >= 0; i--) {
        char tmp[TB_PATH_MAX];
        tb_fs_join(out, chain[i]->cells[0], tmp, sizeof tmp);
        snprintf(out, cap, "%s", tmp);
    }
}

/* ---- browser ------------------------------------------------------------ */

static void load_dir(tb_node *parent, const char *path) {
    int n = 0;
    tb_fs_entry *e = tb_fs_list(path, &n);
    for (int i = 0; i < n; i++) {
        const char *cells[1] = { e[i].name };
        tb_miller_add(browser, parent, cells, 1, NULL, e[i].isdir);
    }
    tb_fs_free(e, n);
}

static void on_expand(tb_app *app, void *data) {
    (void)app;
    (void)data;
    tb_node *n = tb_miller_pending(browser);
    char path[TB_PATH_MAX];
    node_path(n, path, sizeof path);
    load_dir(n, path);
}

static void open_path(tb_app *app, const char *path);

/* Double-click (or Return) on a browser row: open files, ignore folders. */
static void on_browser_open(tb_app *app, void *data) {
    (void)data;
    tb_node *n = tb_miller_selected(browser);
    if (!n || n->expandable) return;
    char path[TB_PATH_MAX];
    node_path(n, path, sizeof path);
    open_path(app, path);
}

/* ---- layout ------------------------------------------------------------- */

static int clampi(int v, int lo, int hi) {
    if (hi < lo) hi = lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Where a document's view goes: inside the frame below the tab strip. */
static void editor_rect(tb_app *app, int *x, int *y, int *w, int *h) {
    int ey = split->base.y + TB_SPLIT_BAR;
    int eh = app->height - CE_STATUS_H - ey;
    int sh = tb_tabs_strip_h(app);
    *x = 1;
    *y = ey + sh;
    *w = app->width - 2;
    *h = eh - sh - 1;
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

static void ce_sync(tb_app *app);

static void layout(tb_app *app) {
    int top = TB_MENUBAR_H;
    split->min_top = top + CE_MIN_BROWSER;
    split->min_bottom = CE_STATUS_H + CE_MIN_EDITOR;
    split->base.x = 0;
    split->base.w = app->width;
    split->base.y = clampi(split->base.y, split->min_top,
                           app->height - TB_SPLIT_BAR - split->min_bottom);

    browser->base.x = 0;
    browser->base.y = top;
    browser->base.w = app->width;
    browser->base.h = split->base.y - top;

    int ey = split->base.y + TB_SPLIT_BAR;
    tabs->base.x = 0;
    tabs->base.y = ey;
    tabs->base.w = app->width;
    tabs->base.h = app->height - CE_STATUS_H - ey;

    status->base.x = 0;
    status->base.y = app->height - CE_STATUS_H;
    status->base.w = app->width;

    int x, y, w, h;
    editor_rect(app, &x, &y, &w, &h);
    for (int i = 0; i < ndocs; i++) {
        if (docs[i]->tv) tb_textview_resize(app, docs[i]->tv, x, y, w, h);
        if (docs[i]->term) tt_panel_place(app, docs[i]->term, x, y, w, h);
    }
    for (int i = 0; i < nspare; i++) tb_textview_resize(app, spare[i], x, y, w, h);
    ce_sync(app);
    tb_damage_all(app);
}

static void on_resize(tb_app *app, void *data) {
    (void)data;
    layout(app);
}

static void on_split(tb_app *app, void *data) {
    (void)data;
    layout(app);
}

/* ---- documents ---------------------------------------------------------- */

static ce_doc *cur_doc(void) {
    int i = tb_tabs_get(tabs);
    return (i >= 0 && i < ndocs) ? docs[i] : NULL;
}

static int doc_index(const ce_doc *d) {
    for (int i = 0; i < ndocs; i++)
        if (docs[i] == d) return i;
    return -1;
}

static int doc_modified(const ce_doc *d) {
    return d->tv && tb_textview_modified(d->tv);
}

/* The file name, "untitled N", or "Teletype". */
static void doc_name(const ce_doc *d, char *out, size_t cap) {
    if (d->term) snprintf(out, cap, "Teletype");
    else if (d->path) snprintf(out, cap, "%s", base_of(d->path));
    else snprintf(out, cap, "untitled %d", d->no);
}

/* Path of the document relative to the browser root, or the full path
 * when it lies outside the root. Untitled documents show their name. */
static void doc_relpath(const ce_doc *d, char *out, size_t cap) {
    if (!d->path) { doc_name(d, out, cap); return; }
    size_t rl = strlen(root);
    if (strncmp(d->path, root, rl) == 0) {
        if (root[rl - 1] == '/') { snprintf(out, cap, "%s", d->path + rl); return; }
        if (d->path[rl] == '/') { snprintf(out, cap, "%s", d->path + rl + 1); return; }
    }
    snprintf(out, cap, "%s", d->path);
}

/* Shorten s from the left, with "..." in front, until it fits in maxw. */
static void fit_tail(tb_app *app, const char *s, int maxw, char *out, size_t cap) {
    snprintf(out, cap, "%s", s);
    if (tb_text_w(app, out) <= maxw || cap < 5) return;
    size_t n = strlen(s);
    for (size_t i = 1; i < n; i++) {
        size_t keep = n - i;
        if (keep > cap - 4) keep = cap - 4;
        memcpy(out, "...", 3);
        memcpy(out + 3, s + i, keep);
        out[3 + keep] = 0;
        if (tb_text_w(app, out) <= maxw) return;
    }
    snprintf(out, cap, "...");
}

/* Tab text: the name, with * when edited. */
static void make_label(const ce_doc *d, char *out, size_t cap) {
    char name[TB_PATH_MAX];
    doc_name(d, name, sizeof name);
    snprintf(out, cap, "%s%s", name, doc_modified(d) ? " *" : "");
}

/* Update tab titles that changed and the window title, status line and
 * menubar path for the current tab. */
static void ce_sync(tb_app *app) {
    char label[TB_PATH_MAX + 16];
    for (int i = 0; i < ndocs; i++) {
        make_label(docs[i], label, sizeof label);
        if (strcmp(label, docs[i]->label) != 0) {
            snprintf(docs[i]->label, sizeof docs[i]->label, "%s", label);
            tb_tabs_rename(tabs, i, label);
        }
    }

    ce_doc *d = cur_doc();
    if (!d) return;
    int edited = doc_modified(d);
    char name[TB_PATH_MAX], title[TB_PATH_MAX + 64], buf[128];
    doc_name(d, name, sizeof name);
    snprintf(title, sizeof title, "%s%s - CodeEdit", name, edited ? " (edited)" : "");
    tb_set_title(app, title);

    if (d->term) {
        tb_status_set(status, "Teletype   /bin/sh");
    } else {
        int line, col;
        tb_textview_cursor(d->tv, &line, &col);
        snprintf(buf, sizeof buf, "Line %d, Col %d      %s",
                 line, col, edited ? "Edited" : "Saved");
        tb_status_set(status, buf);
    }

    /* Menubar: the path of the current tab, room permitting. */
    char rel[TB_PATH_MAX], shown[TB_PATH_MAX];
    doc_relpath(d, rel, sizeof rel);
    int menu_end = 0;
    if (app->nmenus > 0)
        menu_end = app->menus[app->nmenus - 1].x + app->menus[app->nmenus - 1].tw;
    fit_tail(app, rel, app->width - menu_end - 2 * TB_MENU_PAD_X, shown, sizeof shown);
    tb_menu_status(app, shown);
}

static void on_update(tb_app *app, void *data) {
    (void)data;
    ce_sync(app);
}

static tb_textview *take_view(tb_app *app) {
    tb_textview *tv;
    if (nspare > 0) {
        tv = spare[--nspare];
        tb_textview_set(tv, "");
    } else {
        tv = tb_textview_new(app, 0, 0, 1, 1);
    }
    tb_textview_on_update(tv, on_update, NULL);
    int x, y, w, h;
    editor_rect(app, &x, &y, &w, &h);
    tb_textview_resize(app, tv, x, y, w, h);
    return tv;
}

static void return_view(tb_app *app, tb_textview *tv) {
    tb_widget_hide(&tv->base, 1);
    tb_textview_on_update(tv, NULL, NULL);
    tb_textview_set(tv, "");
    tb_textview **slot = tb_push((void **)&spare, &nspare, &cspare, sizeof *slot);
    *slot = tv;
    (void)app;
}

/* Show document i; only its view is visible. */
static void show_doc(tb_app *app, int i) {
    if (i < 0 || i >= ndocs) return;
    for (int j = 0; j < ndocs; j++) {
        int on = (j == i);
        if (docs[j]->tv) tb_widget_hide(&docs[j]->tv->base, !on);
        if (docs[j]->term) tt_panel_show(app, docs[j]->term, on);
    }
    tb_tabs_set(tabs, i);
    ce_doc *d = docs[i];
    app->root.focus = d->tv ? &d->tv->base : &d->term->input->base;
    ce_sync(app);
    tb_damage_all(app);
}

/* Read a file into a text view. A missing file leaves it empty. */
static void load_text(tb_textview *tv, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        tb_textview_set(tv, "");
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); tb_textview_set(tv, ""); return; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    tb_textview_set(tv, buf);
    free(buf);
}

/* Add a document (path may be NULL for untitled) as the last tab. */
static ce_doc *doc_add(tb_app *app, const char *path) {
    ce_doc *d = calloc(1, sizeof *d);
    if (!d) tb_oom();
    d->path = path ? tb_strdup(path) : NULL;
    if (!path) d->no = ++untitled_count;
    d->tv = take_view(app);
    make_label(d, d->label, sizeof d->label);
    ce_doc **slot = tb_push((void **)&docs, &ndocs, &cdocs, sizeof *slot);
    *slot = d;
    tb_tabs_add(tabs, d->label);
    return d;
}

/* Add the fixed Teletype tab. Its shell starts now. */
static ce_doc *doc_add_terminal(tb_app *app) {
    ce_doc *d = calloc(1, sizeof *d);
    if (!d) tb_oom();
    d->term = tt_panel_new(app);
    int x, y, w, h;
    editor_rect(app, &x, &y, &w, &h);
    tt_panel_place(app, d->term, x, y, w, h);
    tt_panel_start(app, d->term);
    make_label(d, d->label, sizeof d->label);
    ce_doc **slot = tb_push((void **)&docs, &ndocs, &cdocs, sizeof *slot);
    *slot = d;
    tb_tabs_add(tabs, d->label);
    return d;
}

/* Close document i. The Teletype tab is never closed, so i is a file tab. */
static void doc_remove(tb_app *app, int i) {
    ce_doc *d = docs[i];
    if (d->term) return;
    return_view(app, d->tv);
    free(d->path);
    free(d);
    memmove(&docs[i], &docs[i + 1], (size_t)(ndocs - i - 1) * sizeof *docs);
    ndocs--;
    tb_tabs_remove(tabs, i);
    show_doc(app, i < ndocs ? i : ndocs - 1);
}

/* ---- saving ------------------------------------------------------------- */

static int write_doc(tb_app *app, ce_doc *d, const char *path) {
    char *s = tb_textview_get(d->tv);
    FILE *f = fopen(path, "wb");
    int ok = 0;
    if (f) {
        ok = fputs(s, f) >= 0;
        if (fclose(f) != 0) ok = 0;
    }
    free(s);
    if (ok) tb_textview_clear_modified(d->tv);
    else tb_alert(app, TB_ALERT_STOP, "Could not save the file.", "OK", NULL, NULL);
    return ok;
}

static int save_as(tb_app *app, ce_doc *d) {
    char dir[TB_PATH_MAX], path[TB_PATH_MAX];
    if (!d->tv) return 0;
    if (d->path) dir_of(d->path, dir, sizeof dir);
    else snprintf(dir, sizeof dir, "%s", root);
    const char *name = d->path ? base_of(d->path) : "untitled.txt";
    if (!tb_save_dialog(app, dir, name, path, sizeof path)) return 0;
    free(d->path);
    d->path = tb_strdup(path);
    int ok = write_doc(app, d, path);
    ce_sync(app);
    return ok;
}

static int save_doc(tb_app *app, ce_doc *d) {
    if (d->term) return 1;
    return d->path ? write_doc(app, d, d->path) : save_as(app, d);
}

/* Ask about unsaved changes before closing d. 1 = go ahead, 0 = cancel. */
static int confirm_doc(tb_app *app, ce_doc *d) {
    if (!doc_modified(d)) return 1;
    char name[TB_PATH_MAX], msg[TB_PATH_MAX + 64];
    doc_name(d, name, sizeof name);
    snprintf(msg, sizeof msg, "Save changes to \"%s\" before closing?", name);
    int r = tb_alert(app, TB_ALERT_CAUTION, msg, "Save", "Discard", "Cancel");
    if (r == 1) return save_doc(app, d);
    if (r == 2) return 1;
    return 0;
}

/* Before quitting: every document with changes must be saved or discarded. */
static int confirm_all(tb_app *app) {
    for (int i = 0; i < ndocs; i++) {
        if (!doc_modified(docs[i])) continue;
        show_doc(app, i);
        if (!confirm_doc(app, docs[i])) return 0;
    }
    return 1;
}

/* ---- opening ------------------------------------------------------------ */

/* Show the document for path, opening it in a new tab if needed. */
static void open_path(tb_app *app, const char *path) {
    for (int i = 0; i < ndocs; i++)
        if (docs[i]->path && strcmp(docs[i]->path, path) == 0) {
            show_doc(app, i);
            return;
        }
    ce_doc *d = doc_add(app, path);
    load_text(d->tv, path);
    show_doc(app, doc_index(d));
}

/* ---- tabs --------------------------------------------------------------- */

static void on_tab(tb_app *app, void *data) {
    (void)data;
    show_doc(app, tb_tabs_get(tabs));
}

/* ---- File menu ---------------------------------------------------------- */

static void file_new(tb_app *app, void *data) {
    (void)data;
    doc_add(app, NULL);
    show_doc(app, ndocs - 1);
}

static void file_open(tb_app *app, void *data) {
    char dir[TB_PATH_MAX], path[TB_PATH_MAX];
    (void)data;
    ce_doc *d = cur_doc();
    if (d && d->path) dir_of(d->path, dir, sizeof dir);
    else snprintf(dir, sizeof dir, "%s", root);
    if (tb_open_dialog(app, dir, path, sizeof path)) open_path(app, path);
}

static void file_save(tb_app *app, void *data) {
    (void)data;
    ce_doc *d = cur_doc();
    if (d) save_doc(app, d);
    ce_sync(app);
}

static void file_save_as(tb_app *app, void *data) {
    (void)data;
    ce_doc *d = cur_doc();
    if (d) save_as(app, d);
    ce_sync(app);
}

static void file_close(tb_app *app, void *data) {
    (void)data;
    int i = tb_tabs_get(tabs);
    if (i < 0 || i >= ndocs || docs[i]->term) return;
    if (confirm_doc(app, docs[i])) doc_remove(app, i);
    ce_sync(app);
}

static void file_quit(tb_app *app, void *data) {
    (void)data;
    if (confirm_all(app)) tb_quit(app);
}

/* Window close button: same check as Quit. */
static int on_close(tb_app *app, void *data) {
    (void)data;
    return confirm_all(app);
}

/* ---- Edit menu ---------------------------------------------------------- */

static void edit_cut(tb_app *app, void *data) {
    (void)data;
    ce_doc *d = cur_doc();
    if (d && d->tv) tb_textview_cut(app, d->tv);
}

static void edit_copy(tb_app *app, void *data) {
    (void)data;
    ce_doc *d = cur_doc();
    if (d && d->tv) tb_textview_copy(app, d->tv);
}

static void edit_paste(tb_app *app, void *data) {
    (void)data;
    tb_clip_paste(app);
}

static void edit_select_all(tb_app *app, void *data) {
    (void)data;
    ce_doc *d = cur_doc();
    if (d && d->tv) tb_textview_select_all(app, d->tv);
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv) {
    char abs[TB_PATH_MAX], file_arg[TB_PATH_MAX] = "";
    const char *arg = argc > 1 ? argv[1] : ".";
    if (!realpath(arg, abs)) {
        fprintf(stderr, "codeedit: cannot open %s\n", arg);
        return 1;
    }
    struct stat st;
    if (stat(abs, &st) != 0) {
        fprintf(stderr, "codeedit: cannot open %s\n", arg);
        return 1;
    }
    if (S_ISDIR(st.st_mode)) {
        snprintf(root, sizeof root, "%s", abs);
    } else {
        snprintf(file_arg, sizeof file_arg, "%s", abs);
        dir_of(abs, root, sizeof root);
    }

    /* A write to a dead shell must fail with EPIPE, not kill us. */
    signal(SIGPIPE, SIG_IGN);

    tb_app *app = tb_app_new("CodeEdit", CE_W, CE_H);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "New", file_new, NULL);
    tb_menu_item(app, "Open...", file_open, NULL);
    tb_menu_item(app, "Save", file_save, NULL);
    tb_menu_item(app, "Save As...", file_save_as, NULL);
    tb_menu_item(app, "-", NULL, NULL);
    tb_menu_item(app, "Close Tab", file_close, NULL);
    tb_menu_item(app, "-", NULL, NULL);
    tb_menu_item(app, "Quit", file_quit, NULL);

    tb_menu_new(app, "Edit");
    tb_menu_item(app, "Cut", edit_cut, NULL);
    tb_menu_item(app, "Copy", edit_copy, NULL);
    tb_menu_item(app, "Paste", edit_paste, NULL);
    tb_menu_item(app, "-", NULL, NULL);
    tb_menu_item(app, "Select All", edit_select_all, NULL);

    /* Browser on top, splitter, tab strip with the tabs, status line. The
     * tab strip is added before any tab's widgets so they are hit first. */
    int top = TB_MENUBAR_H, total = CE_H - top - CE_STATUS_H;
    int split_y = top + total * 2 / 5;
    browser = tb_miller_new(app, 0, top, CE_W, split_y - top, CE_COL_W);
    tb_miller_on_expand(browser, on_expand, NULL);
    tb_miller_on_open(browser, on_browser_open, NULL);
    split = tb_split_new(app, 0, split_y, CE_W, top + CE_MIN_BROWSER,
                         CE_STATUS_H + CE_MIN_EDITOR, on_split, NULL);
    tabs = tb_tabs_new(app, 0, split_y + TB_SPLIT_BAR, CE_W,
                       CE_H - CE_STATUS_H - split_y - TB_SPLIT_BAR, on_tab, NULL);
    status = tb_status_new(app, 0, CE_H - CE_STATUS_H, CE_W, CE_STATUS_H, "");

    tb_on_close(app, on_close, NULL);
    tb_on_resize(app, on_resize, NULL);
    layout(app);
    load_dir(NULL, root);

    /* The Teletype tab is always first. Files open after it. */
    doc_add_terminal(app);
    if (file_arg[0]) open_path(app, file_arg);
    else show_doc(app, 0);

    tb_run(app);

    for (int i = 0; i < ndocs; i++)
        if (docs[i]->term) tt_panel_stop(docs[i]->term);
    tb_app_free(app);
    return 0;
}
