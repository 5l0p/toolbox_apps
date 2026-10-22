#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"

/* Example: a System 6/7 style file browser. Two views of the same folder:
 * Tree (disclosure triangles, Name/Size/Kind columns) and Columns (Miller). */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FB_W 520
#define FB_H 350
#define FB_COL_W 170

static tb_view *tree;
static tb_miller *cols;
static int use_cols;                 /* 0 = Tree, 1 = Columns */
static int show_hidden;             /* dot-files listed when set */
static int view_menu, hidden_item;  /* for relabelling the toggle */
static tb_label *path_label;
static char root_path[PATH_MAX];

/* ---- helpers ----------------------------------------------------------- */

/* Full path of a node: root_path plus the names of its ancestors. Works for
 * both views: their pseudo roots have depth -1. */
static void node_path(const tb_node *n, char *out, size_t cap) {
    const tb_node *chain[64];
    int k = 0;
    for (const tb_node *p = n; p && p->depth >= 0 && k < 64; p = p->parent)
        chain[k++] = p;
    snprintf(out, cap, "%s", root_path);
    for (int i = k - 1; i >= 0; i--) {
        char tmp[PATH_MAX];
        tb_fs_join(out, chain[i]->cells[0], tmp, sizeof tmp);
        snprintf(out, cap, "%s", tmp);
    }
}

/* Add the entries of path under parent (NULL = top level) to the visible
 * view. Folders are expandable; the views load them lazily. */
static void load_dir(tb_node *parent, const char *path) {
    int n = 0;
    tb_fs_entry *e = tb_fs_list(path, &n);
    for (int i = 0; i < n; i++) {
        if (!show_hidden && e[i].name[0] == '.') continue;
        char size[32] = "", kind[64];
        if (!e[i].isdir) tb_fs_format_size(e[i].size, size, sizeof size);
        tb_fs_kind(e[i].name, e[i].isdir, kind, sizeof kind);
        const char *cells[3] = { e[i].name, size, kind };
        if (use_cols) {
            tb_miller_add(cols, parent, cells, 3, NULL, e[i].isdir);
        } else {
            tb_node *nd = tb_tree_add(tree, parent, cells, 3, NULL, e[i].isdir);
            tb_view_set_key(nd, 1, (double)e[i].size);
        }
    }
    tb_fs_free(e, n);
}

/* ---- navigation -------------------------------------------------------- */

static void clear_views(void) {
    tb_view_clear(tree);
    tb_miller_clear(cols);
}

/* Make path the current folder: chdir into it, then take the real working
 * directory as root_path. Relative paths such as ".." are resolved by the
 * kernel, so go_to(app, "..") is the Up command. */
static void go_to(tb_app *app, const char *path) {
    if (chdir(path) != 0 || !getcwd(root_path, sizeof root_path)) {
        tb_menu_status(app, "Cannot open folder");
        return;
    }
    clear_views();
    load_dir(NULL, root_path);
    tb_label_set(app, path_label, root_path);
    tb_menu_status(app, "");
    tb_damage_all(app);
}

static void go_up(tb_app *app) {
    go_to(app, "..");
}

/* Switch between the Tree and Columns views, showing the active one. */
static void set_view(tb_app *app, int columns) {
    use_cols = columns;
    tb_widget_hide(&tree->base, columns);
    tb_widget_hide(&cols->base, !columns);
    app->root.focus = columns ? &cols->base : &tree->base;
    go_to(app, root_path);
    tb_menu_status(app, columns ? "Columns view" : "Tree view");
}

/* ---- callbacks --------------------------------------------------------- */

static void on_expand(tb_app *app, void *data) {
    (void)app;
    (void)data;
    tb_node *n = tb_view_pending(tree);
    char path[PATH_MAX];
    node_path(n, path, sizeof path);
    load_dir(n, path);
}

static void on_col_expand(tb_app *app, void *data) {
    (void)app;
    (void)data;
    tb_node *n = tb_miller_pending(cols);
    char path[PATH_MAX];
    node_path(n, path, sizeof path);
    load_dir(n, path);
}

static void on_select(tb_app *app, void *data) {
    (void)data;
    tb_node *n = use_cols ? tb_miller_selected(cols) : tb_view_selected(tree);
    if (!n) return;
    char path[PATH_MAX];
    node_path(n, path, sizeof path);
    tb_menu_status(app, path);
}

static void on_open(tb_app *app, void *data) {
    (void)data;
    if (use_cols) {
        /* Columns: a folder is already open in the next column, so
         * double-clicking it goes into it instead. */
        tb_node *n = tb_miller_selected(cols);
        if (!n) return;
        char path[PATH_MAX];
        node_path(n, path, sizeof path);
        if (n->expandable) go_to(app, path);
        else {
            char msg[PATH_MAX + 16];
            snprintf(msg, sizeof msg, "Opened: %s", path);
            tb_menu_status(app, msg);
        }
        return;
    }

    tb_node *n = tb_view_selected(tree);
    if (!n) return;
    if (n->expandable) {
        tb_view_expand(app, tree, n, !n->expanded);
        return;
    }
    char path[PATH_MAX], msg[PATH_MAX + 16];
    node_path(n, path, sizeof path);
    snprintf(msg, sizeof msg, "Opened: %s", path);
    tb_menu_status(app, msg);
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

static void menu_tree(tb_app *app, void *data) {
    (void)data;
    if (use_cols) set_view(app, 0);
}

static void menu_columns(tb_app *app, void *data) {
    (void)data;
    if (!use_cols) set_view(app, 1);
}

static void menu_hidden(tb_app *app, void *data) {
    (void)data;
    show_hidden = !show_hidden;
    tb_menu_set_item(app, view_menu, hidden_item,
                     show_hidden ? "Hide Hidden Files" : "Show Hidden Files");
    go_to(app, root_path);
}

/* ---- main -------------------------------------------------------------- */

/* The browser fills the window below the menubar. */
static void layout(tb_app *app) {
    /* Both views fill the window below the menubar, edge to edge. */
    tb_widget *views[2] = { &tree->base, &cols->base };
    for (int i = 0; i < 2; i++) {
        views[i]->x = 0;
        views[i]->y = TB_MENUBAR_H;
        views[i]->w = app->width;
        views[i]->h = app->height - TB_MENUBAR_H;
    }
    tb_damage_all(app);
}

static void on_resize(tb_app *app, void *data) {
    (void)data;
    layout(app);
}

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "usage: %s [folder]\n", argv[0]);
        return 2;
    }
    const char *start = argc > 1 ? argv[1] : getenv("HOME");
    if (!start) start = ".";
    if (chdir(start) != 0 || !getcwd(root_path, sizeof root_path)) {
        fprintf(stderr, "filebrowser: cannot open %s: %s\n", start, strerror(errno));
        return 1;
    }

    tb_app *app = tb_app_new("Files", FB_W, FB_H);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "Quit", menu_quit, NULL);

    tb_menu_new(app, "Go");
    tb_menu_item(app, "Up", menu_up, NULL);
    tb_menu_item(app, "Home", menu_home, NULL);

    view_menu = tb_menu_new(app, "View");
    tb_menu_item(app, "Tree", menu_tree, NULL);
    tb_menu_item(app, "Columns", menu_columns, NULL);
    tb_menu_item(app, "-", NULL, NULL);
    hidden_item = tb_menu_item(app, "Show Hidden Files", menu_hidden, NULL);

    path_label = tb_label_new(app, 142, 31, TB_BLACK, root_path);

    const char *titles[3] = { "Name", "Size", "Kind" };
    const int widths[3] = { 260, 90, 130 };
    tree = tb_tree_new(app, 0, TB_MENUBAR_H, FB_W, FB_H - TB_MENUBAR_H, 3, titles, widths);
    tb_view_folders_first(tree, 1);
    tb_view_sort(tree, 0, 0);            /* by name, ascending */
    tb_view_on_select(tree, on_select, tree);
    tb_view_on_open(tree, on_open, tree);
    tb_view_on_expand(tree, on_expand, tree);

    cols = tb_miller_new(app, 0, TB_MENUBAR_H, FB_W, FB_H - TB_MENUBAR_H, FB_COL_W);
    tb_miller_on_select(cols, on_select, cols);
    tb_miller_on_open(cols, on_open, cols);
    tb_miller_on_expand(cols, on_col_expand, cols);

    tb_on_resize(app, on_resize, NULL);
    set_view(app, 1);                    /* Columns is the default */
    tb_run(app);
    tb_app_free(app);
    return 0;
}
