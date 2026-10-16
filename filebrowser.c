#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"

/* Example: a System 6/7 style file browser built on tb_tree. */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FB_W 520
#define FB_H 350

static tb_view *view;
static tb_label *path_label;
static tb_label *status_label;
static char root_path[PATH_MAX];

/* ---- helpers ----------------------------------------------------------- */

static void path_join(const char *dir, const char *name, char *out, size_t cap) {
    size_t l = strlen(dir);
    if (l > 0 && dir[l - 1] == '/') snprintf(out, cap, "%s%s", dir, name);
    else snprintf(out, cap, "%s/%s", dir, name);
}

/* Full path of a node: root_path plus the names of its ancestors. */
static void node_path(const tb_node *n, char *out, size_t cap) {
    const tb_node *chain[64];
    int k = 0;
    for (const tb_node *p = n; p && p->depth >= 0 && k < 64; p = p->parent)
        chain[k++] = p;
    snprintf(out, cap, "%s", root_path);
    for (int i = k - 1; i >= 0; i--) {
        char tmp[PATH_MAX];
        path_join(out, chain[i]->cells[0], tmp, sizeof tmp);
        snprintf(out, cap, "%s", tmp);
    }
}

static int name_cmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

typedef struct {
    char *name;
    int isdir;
    long long size;
} fb_entry;

/* Folders first, then by name (case-insensitive). */
static int entry_cmp(const void *pa, const void *pb) {
    const fb_entry *a = pa, *b = pb;
    if (a->isdir != b->isdir) return b->isdir - a->isdir;
    return name_cmp(a->name, b->name);
}

static void format_size(long long n, char *out, size_t cap) {
    if (n < 1024)              snprintf(out, cap, "%lld bytes", n);
    else if (n < 1024 * 1024)  snprintf(out, cap, "%.1f KB", n / 1024.0);
    else                       snprintf(out, cap, "%.1f MB", n / (1024.0 * 1024.0));
}

static void kind_of(const char *name, int isdir, char *out, size_t cap) {
    if (isdir) { snprintf(out, cap, "Folder"); return; }
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name || !dot[1]) { snprintf(out, cap, "Document"); return; }
    char ext[32];
    size_t i = 0;
    for (; dot[1 + i] && i + 1 < sizeof ext; i++)
        ext[i] = (char)toupper((unsigned char)dot[1 + i]);
    ext[i] = 0;
    snprintf(out, cap, "%s document", ext);
}

/* Read one directory and add its entries under parent (NULL = top level). */
static void load_dir(tb_node *parent, const char *path) {
    DIR *d = opendir(path);
    if (!d) return;

    size_t n = 0, cap = 0;
    fb_entry *e = NULL;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char full[PATH_MAX];
        path_join(path, de->d_name, full, sizeof full);
        struct stat st;
        if (stat(full, &st) != 0) { st.st_mode = 0; st.st_size = 0; }

        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            e = realloc(e, cap * sizeof *e);
            if (!e) tb_oom();
        }
        e[n].name = tb_strdup(de->d_name);
        e[n].isdir = S_ISDIR(st.st_mode) ? 1 : 0;
        e[n].size = (long long)st.st_size;
        n++;
    }
    closedir(d);

    qsort(e, n, sizeof *e, entry_cmp);

    for (size_t i = 0; i < n; i++) {
        char size[32], kind[64];
        if (e[i].isdir) size[0] = 0;
        else format_size(e[i].size, size, sizeof size);
        kind_of(e[i].name, e[i].isdir, kind, sizeof kind);
        const char *cells[3] = { e[i].name, size, kind };
        tb_node *nd = tb_tree_add(view, parent, cells, 3, NULL, e[i].isdir);
        tb_view_set_key(nd, 1, (double)e[i].size);
        free(e[i].name);
    }
    free(e);
}

/* ---- navigation -------------------------------------------------------- */

static void go_to(tb_app *app, const char *path) {
    snprintf(root_path, sizeof root_path, "%s", path);
    tb_view_clear(view);
    load_dir(NULL, root_path);
    tb_label_set(app, path_label, root_path);
    tb_label_set(app, status_label, "");
}

static void go_up(tb_app *app) {
    char *slash = strrchr(root_path, '/');
    if (!slash) return;
    if (slash == root_path) root_path[1] = 0;   /* "/home" -> "/" */
    else *slash = 0;
    go_to(app, root_path);
}

/* ---- callbacks --------------------------------------------------------- */

static void on_expand(tb_app *app, void *data) {
    (void)app;
    tb_view *v = data;
    tb_node *n = tb_view_pending(v);
    char path[PATH_MAX];
    node_path(n, path, sizeof path);
    load_dir(n, path);
}

static void on_select(tb_app *app, void *data) {
    tb_view *v = data;
    tb_node *n = tb_view_selected(v);
    if (!n) return;
    char path[PATH_MAX];
    node_path(n, path, sizeof path);
    tb_label_set(app, status_label, path);
}

static void on_open(tb_app *app, void *data) {
    tb_view *v = data;
    tb_node *n = tb_view_selected(v);
    if (!n) return;
    if (n->expandable) {
        tb_view_expand(app, v, n, !n->expanded);
        return;
    }
    char path[PATH_MAX], msg[PATH_MAX + 16];
    node_path(n, path, sizeof path);
    snprintf(msg, sizeof msg, "Opened: %s", path);
    tb_label_set(app, status_label, msg);
}

static void menu_quit(tb_app *app, void *data) {
    (void)data;
    tb_quit(app);
}

static void menu_up(tb_app *app, void *data) {
    (void)data;
    go_up(app);
}

static void menu_home(tb_app *app, void *data) {
    (void)data;
    const char *home = getenv("HOME");
    if (home) go_to(app, home);
}


/* ---- main -------------------------------------------------------------- */

/* The file list fills the window below the path row; the status line sits
 * at the bottom. */
static void layout(tb_app *app) {
    view->base.w = app->width - 16;
    view->base.h = app->height - 80;
    status_label->base.y = app->height - 20;
    tb_damage_all(app);
}

static void on_resize(tb_app *app, void *data) {
    (void)data;
    layout(app);
}

int main(int argc, char **argv) {
    const char *start = argc > 1 ? argv[1] : getenv("HOME");
    if (!start) start = ".";
    if (!realpath(start, root_path))
        snprintf(root_path, sizeof root_path, "%s", start);

    tb_app *app = tb_app_new("Files", FB_W, FB_H);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "Quit", menu_quit, NULL);

    tb_menu_new(app, "Go");
    tb_menu_item(app, "Up", menu_up, NULL);
    tb_menu_item(app, "Home", menu_home, NULL);

    path_label = tb_label_new(app, 142, 31, TB_BLACK, root_path);

    const char *titles[3] = { "Name", "Size", "Kind" };
    const int widths[3] = { 260, 90, 130 };
    view = tb_tree_new(app, 0, 20, FB_W , FB_H - 20 - 20 , 3, titles, widths);
    tb_view_folders_first(view, 1);
    tb_view_sort(view, 0, 0);            /* by name, ascending */
    tb_view_on_select(view, on_select, view);
    tb_view_on_open(view, on_open, view);
    tb_view_on_expand(view, on_expand, view);

    status_label = tb_label_new(app, 8, 330, TB_NAVY, "");

    tb_on_resize(app, on_resize, NULL);
    go_to(app, root_path);
    tb_run(app);
    tb_app_free(app);
    return 0;
}
