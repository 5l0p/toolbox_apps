/*
 * tb_lua.c - run a Lua script as the logic of a toolbox application.
 *
 * The host owns the window, menubar and widgets. The script builds the
 * interface through the `ui` table and binds Lua functions as handlers:
 *
 *   ui.title("Notes")
 *   ui.menu("File")
 *   ui.item("Open...", function()
 *       local path = ui.open_file(".")
 *       if path then ui.set(status, "Opened " .. path) end
 *   end)
 *   ui.item("-")
 *   ui.item("Quit", function() ui.quit() end)
 *
 *   status = ui.status(0, 260, 480, 20, "Ready")
 *   ui.button(16, 60, 90, 24, "Hello", function()
 *       ui.alert(ui.NOTE, "Hello from Lua", "OK")
 *   end)
 *
 * Build:  make tb_lua
 * Run:    ./tb_lua script.lua
 *
 * Lua functions are kept in the registry and called with lua_pcall, so an
 * error in a handler is printed to stderr and does not stop the window.
 * Handles returned by ui.label / ui.button / ui.status are small integers
 * (1-based) that stay valid for the life of the program.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"

#include "deps/lua/lua.h"
#include "deps/lua/lauxlib.h"
#include "deps/lua/lualib.h"

#define TL_MAX_WIDGETS 256
#define TL_PATH_MAX    4096

enum { TL_LABEL = 1, TL_BUTTON, TL_STATUS, TL_LIST, TL_FIELD, TL_SLIDER, TL_CHECK };

typedef struct {
    int kind;
    void *w;
} tl_widget;

static lua_State *L;
static tb_app *app;
static tl_widget widgets[TL_MAX_WIDGETS];
static int nwidgets;

/* ---- helpers ------------------------------------------------------------ */

/* Store the function at stack index `idx` in the registry; return its ref. */
static int tl_ref(lua_State *L, int idx) {
    lua_pushvalue(L, idx);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

/* Call the registered function `ref` with no arguments. Errors are reported
 * to stderr; they must not unwind through the toolbox event loop. */
static void tl_fire(int ref) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

/* Call the registered function `ref` with one integer argument. */
static void tl_fire_int(int ref, int arg) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, arg);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void tl_menu_cb(tb_app *t, void *data) {
    (void)t;
    tl_fire((int)(intptr_t)data);
}

/* Optional function argument: returns a registry ref, or 0 for nil. */
static int tl_opt_ref(lua_State *L, int idx) {
    if (lua_isnoneornil(L, idx)) return 0;
    luaL_checktype(L, idx, LUA_TFUNCTION);
    return tl_ref(L, idx);
}

static void tl_button_cb(tb_app *t, void *data) {
    (void)t;
    if (data) tl_fire((int)(intptr_t)data);
}

static int tl_new_handle(int kind, void *w) {
    if (nwidgets >= TL_MAX_WIDGETS)
        luaL_error(L, "too many widgets (limit %d)", TL_MAX_WIDGETS);
    widgets[nwidgets].kind = kind;
    widgets[nwidgets].w = w;
    return ++nwidgets;
}

/* List widget state. Rows are numbered from 1; the number is stored in
 * each node's data pointer and passed to the open handler. */
typedef struct {
    tb_view *v;
    int open_ref;         /* 0 = no handler */
    int nrows;
} tl_list;

static void tl_list_open(tb_app *t, void *data) {
    tl_list *tl = data;
    (void)t;
    tb_node *n = tb_view_selected(tl->v);
    if (n && tl->open_ref)
        tl_fire_int(tl->open_ref, (int)(intptr_t)n->data);
}

/* Read a Lua array of strings at absolute index `idx`. The strings stay on
 * the Lua stack (so the pointers remain valid); the caller restores the
 * stack with lua_settop. */
static int tl_strings(lua_State *L, int idx, const char **out, int max) {
    int n = (int)luaL_len(L, idx);
    if (n > max) luaL_error(L, "too many entries (limit %d)", max);
    for (int i = 0; i < n; i++) {
        lua_geti(L, idx, i + 1);
        out[i] = luaL_checkstring(L, -1);
    }
    return n;
}

/* Controls that report changes to a Lua function. `w` is the toolbox widget. */
typedef struct {
    void *w;
    int ref;              /* 0 = no handler */
} tl_ctl;

static void tl_report(void) {
    fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
}

static void tl_field_cb(tb_app *t, void *data) {
    tl_ctl *c = data;
    (void)t;
    if (!c->ref) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->ref);
    lua_pushstring(L, tb_field_get(c->w));
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) tl_report();
}

static void tl_slider_cb(tb_app *t, void *data) {
    tl_ctl *c = data;
    (void)t;
    if (!c->ref) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->ref);
    lua_pushinteger(L, tb_slider_get(c->w));
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) tl_report();
}

static void tl_check_cb(tb_app *t, void *data) {
    tl_ctl *c = data;
    (void)t;
    if (!c->ref) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->ref);
    lua_pushboolean(L, tb_checkbox_get(c->w));
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) tl_report();
}

static tl_widget *tl_get(lua_State *L, int idx) {
    lua_Integer h = luaL_checkinteger(L, idx);
    if (h < 1 || h > nwidgets)
        luaL_argerror(L, idx, "invalid widget handle");
    return &widgets[h - 1];
}

/* ---- ui.* --------------------------------------------------------------- */

static int l_title(lua_State *L) {
    tb_set_title(app, luaL_checkstring(L, 1));
    return 0;
}

static int l_menu(lua_State *L) {
    tb_menu_new(app, luaL_checkstring(L, 1));
    return 0;
}

/* ui.item(title [, fn]) - "-" makes a separator. Appends to the last menu. */
static int l_item(lua_State *L) {
    const char *title = luaL_checkstring(L, 1);
    if (app->nmenus == 0)
        return luaL_error(L, "ui.item called before ui.menu");
    if (strcmp(title, "-") == 0) {
        tb_menu_item(app, title, NULL, NULL);
        return 0;
    }
    int ref = tl_opt_ref(L, 2);
    tb_menu_item(app, title, ref ? tl_menu_cb : NULL, (void *)(intptr_t)ref);
    return 0;
}

/* ui.label(x, y, text [, colour]) -> handle */
static int l_label(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    int colour = (int)luaL_optinteger(L, 4, TB_BLACK);
    tb_label *lbl = tb_label_new(app, x, y, colour, text);
    lua_pushinteger(L, tl_new_handle(TL_LABEL, lbl));
    return 1;
}

/* ui.button(x, y, w, h, text, fn) -> handle */
static int l_button(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    const char *text = luaL_checkstring(L, 5);
    int ref = tl_opt_ref(L, 6);
    tb_button *b = tb_button_new(app, x, y, w, h, text,
                                 ref ? tl_button_cb : NULL, (void *)(intptr_t)ref);
    lua_pushinteger(L, tl_new_handle(TL_BUTTON, b));
    return 1;
}

/* ui.status(x, y, w, h, text) -> handle */
static int l_status(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    const char *text = luaL_checkstring(L, 5);
    tb_status *s = tb_status_new(app, x, y, w, h, text);
    lua_pushinteger(L, tl_new_handle(TL_STATUS, s));
    return 1;
}

/* ui.set(handle, text) - changes the text of a label or status bar. */
static int l_set(lua_State *L) {
    tl_widget *tw = tl_get(L, 1);
    const char *text = luaL_checkstring(L, 2);
    if (tw->kind == TL_LABEL)
        tb_label_set(app, tw->w, text);
    else if (tw->kind == TL_STATUS)
        tb_status_set(tw->w, text);
    else
        return luaL_argerror(L, 1, "handle has no text");
    return 0;
}

/* ui.alert(kind, message [, b1 [, b2 [, b3]]]) -> index of button pressed */
static int l_alert(lua_State *L) {
    int kind = (int)luaL_checkinteger(L, 1);
    const char *msg = luaL_checkstring(L, 2);
    const char *b[3] = { NULL, NULL, NULL };
    for (int i = 0; i < 3; i++)
        if (!lua_isnoneornil(L, 3 + i)) b[i] = luaL_checkstring(L, 3 + i);
    lua_pushinteger(L, tb_alert(app, kind, msg, b[0], b[1], b[2]));
    return 1;
}

/* ui.open_file([start]) -> path or nil */
static int l_open_file(lua_State *L) {
    char out[TL_PATH_MAX];
    const char *start = luaL_optstring(L, 1, ".");
    if (tb_open_dialog(app, start, out, sizeof out)) lua_pushstring(L, out);
    else lua_pushnil(L);
    return 1;
}

/* ui.save_file([start [, name]]) -> path or nil */
static int l_save_file(lua_State *L) {
    char out[TL_PATH_MAX];
    const char *start = luaL_optstring(L, 1, ".");
    const char *name = luaL_optstring(L, 2, "");
    if (tb_save_dialog(app, start, name, out, sizeof out)) lua_pushstring(L, out);
    else lua_pushnil(L);
    return 1;
}

#define TL_MAX_COLS 8

/* Read parallel title and width tables (absolute indices). Strings stay on
 * the stack; the caller restores it. Returns the column count. */
static int tl_columns(lua_State *L, int tidx, int widx, const char **titles, int *widths) {
    int ncols = tl_strings(L, tidx, titles, TL_MAX_COLS);
    if (luaL_len(L, widx) != ncols)
        luaL_error(L, "titles and widths differ in length");
    for (int i = 0; i < ncols; i++) {
        lua_geti(L, widx, i + 1);
        widths[i] = (int)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    return ncols;
}

/* ui.list(x, y, w, h, titles, widths [, onopen]) -> handle
 * onopen(row) runs on double-click or Return. */
static int l_list(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    luaL_checktype(L, 5, LUA_TTABLE);
    luaL_checktype(L, 6, LUA_TTABLE);
    int top = lua_gettop(L);

    const char *titles[TL_MAX_COLS];
    int widths[TL_MAX_COLS];
    int ncols = tl_columns(L, 5, 6, titles, widths);

    tl_list *tl = calloc(1, sizeof *tl);
    if (!tl) tb_oom();
    tl->v = tb_list_new(app, x, y, w, h, ncols, titles, widths);
    tl->open_ref = tl_opt_ref(L, 7);
    tb_view_on_open(tl->v, tl_list_open, tl);
    lua_settop(L, top);

    lua_pushinteger(L, tl_new_handle(TL_LIST, tl));
    return 1;
}

/* ui.list_columns(handle, titles, widths) - change the column layout.
 * Removes all rows. */
static int l_list_columns(lua_State *L) {
    tl_widget *tw = tl_get(L, 1);
    if (tw->kind != TL_LIST) return luaL_argerror(L, 1, "not a list");
    tl_list *tl = tw->w;
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);
    int top = lua_gettop(L);

    const char *titles[TL_MAX_COLS];
    int widths[TL_MAX_COLS];
    int ncols = tl_columns(L, 2, 3, titles, widths);
    tb_view_set_columns(app, tl->v, ncols, titles, widths);
    tl->nrows = 0;
    lua_settop(L, top);
    return 0;
}

/* ui.list_add(handle, cells) -> row number */
static int l_list_add(lua_State *L) {
    tl_widget *tw = tl_get(L, 1);
    if (tw->kind != TL_LIST) return luaL_argerror(L, 1, "not a list");
    tl_list *tl = tw->w;
    luaL_checktype(L, 2, LUA_TTABLE);
    int top = lua_gettop(L);

    const char *cells[TL_MAX_COLS];
    int n = tl_strings(L, 2, cells, TL_MAX_COLS);
    tb_node *node = tb_list_add(tl->v, cells, n, NULL);
    node->data = (void *)(intptr_t)(++tl->nrows);
    lua_settop(L, top);

    lua_pushinteger(L, tl->nrows);
    return 1;
}

/* ui.list_clear(handle) */
static int l_list_clear(lua_State *L) {
    tl_widget *tw = tl_get(L, 1);
    if (tw->kind != TL_LIST) return luaL_argerror(L, 1, "not a list");
    tl_list *tl = tw->w;
    tb_view_clear(tl->v);
    tl->nrows = 0;
    return 0;
}

/* The toolbox widget behind a handle (every widget starts with a tb_widget). */
static tb_widget *tl_base(const tl_widget *tw) {
    switch (tw->kind) {
    case TL_LIST:
        return &((tl_list *)tw->w)->v->base;
    case TL_FIELD: case TL_SLIDER: case TL_CHECK:
        return (tb_widget *)((tl_ctl *)tw->w)->w;
    default:
        return tw->w;
    }
}

/* ui.field(x, y, w, h [, text [, onaccept]]) -> handle. onaccept(text) runs on Return. */
static int l_field(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    const char *text = luaL_optstring(L, 5, "");
    tl_ctl *c = calloc(1, sizeof *c);
    if (!c) tb_oom();
    c->ref = tl_opt_ref(L, 6);
    tb_field *f = tb_field_new(app, x, y, w, h);
    tb_field_set(f, text);
    if (c->ref) tb_field_on_accept(f, tl_field_cb, c);
    c->w = f;
    lua_pushinteger(L, tl_new_handle(TL_FIELD, c));
    return 1;
}

/* ui.slider(x, y, w, h, min, max, value, onchange) -> handle (horizontal) */
static int l_slider(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    int min = (int)luaL_checkinteger(L, 5);
    int max = (int)luaL_checkinteger(L, 6);
    int value = (int)luaL_checkinteger(L, 7);
    tl_ctl *c = calloc(1, sizeof *c);
    if (!c) tb_oom();
    c->ref = tl_opt_ref(L, 8);
    c->w = tb_slider_new(app, x, y, w, h, 0, min, max, value, tl_slider_cb, c);
    lua_pushinteger(L, tl_new_handle(TL_SLIDER, c));
    return 1;
}

/* ui.checkbox(x, y, label, checked [, onchange]) -> handle. onchange(bool) */
static int l_checkbox(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    const char *label = luaL_checkstring(L, 3);
    int checked = lua_toboolean(L, 4);
    tl_ctl *c = calloc(1, sizeof *c);
    if (!c) tb_oom();
    c->ref = tl_opt_ref(L, 5);
    c->w = tb_checkbox_new(app, x, y, label, tl_check_cb, c);
    tb_checkbox_set(c->w, checked);
    lua_pushinteger(L, tl_new_handle(TL_CHECK, c));
    return 1;
}

/* ui.get(handle) -> text (field), number (slider) or boolean (checkbox) */
static int l_get(lua_State *L) {
    tl_widget *tw = tl_get(L, 1);
    tl_ctl *c = tw->w;
    switch (tw->kind) {
    case TL_FIELD:  lua_pushstring(L, tb_field_get(c->w)); return 1;
    case TL_SLIDER: lua_pushinteger(L, tb_slider_get(c->w)); return 1;
    case TL_CHECK:  lua_pushboolean(L, tb_checkbox_get(c->w)); return 1;
    default: return luaL_argerror(L, 1, "handle has no value");
    }
}

/* ui.put(handle, value) - sets a field, slider or checkbox without firing its handler */
static int l_put(lua_State *L) {
    tl_widget *tw = tl_get(L, 1);
    tl_ctl *c = tw->w;
    switch (tw->kind) {
    case TL_FIELD:  tb_field_set(c->w, luaL_checkstring(L, 2)); return 0;
    case TL_SLIDER: tb_slider_set(c->w, (int)luaL_checkinteger(L, 2)); return 0;
    case TL_CHECK:  tb_checkbox_set(c->w, lua_toboolean(L, 2)); return 0;
    default: return luaL_argerror(L, 1, "handle has no value");
    }
}

/* ui.visible(handle, flag) - show or hide a widget (hidden widgets are not drawn) */
static int l_visible(lua_State *L) {
    tl_widget *tw = tl_get(L, 1);
    tb_widget_hide(tl_base(tw), !lua_toboolean(L, 2));
    return 0;
}

/* ui.menubar_status(text) - display-only text at the right of the menubar */
static int l_menubar_status(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    tb_menu_status(app, text);
    return 0;
}

static int l_quit(lua_State *L) {
    (void)L;
    tb_quit(app);
    return 0;
}

static const luaL_Reg ui_funcs[] = {
    { "title",     l_title },
    { "menu",      l_menu },
    { "item",      l_item },
    { "label",     l_label },
    { "button",    l_button },
    { "status",    l_status },
    { "set",       l_set },
    { "menubar_status", l_menubar_status },
    { "field",     l_field },
    { "slider",    l_slider },
    { "checkbox",  l_checkbox },
    { "get",       l_get },
    { "put",       l_put },
    { "visible",   l_visible },
    { "list",      l_list },
    { "list_add",  l_list_add },
    { "list_clear", l_list_clear },
    { "list_columns", l_list_columns },
    { "alert",     l_alert },
    { "open_file", l_open_file },
    { "save_file", l_save_file },
    { "quit",      l_quit },
    { NULL, NULL }
};

static void tl_register_ui(lua_State *L) {
    luaL_newlib(L, ui_funcs);
    lua_pushinteger(L, TB_ALERT_STOP);    lua_setfield(L, -2, "STOP");
    lua_pushinteger(L, TB_ALERT_NOTE);    lua_setfield(L, -2, "NOTE");
    lua_pushinteger(L, TB_ALERT_CAUTION); lua_setfield(L, -2, "CAUTION");
    /* Palette colours from config.h, e.g. ui.NAVY, ui.RED. */
    static const struct { const char *name; int value; } colours[] = {
        { "BLACK", TB_BLACK },   { "MAROON", TB_MAROON }, { "GREEN", TB_GREEN },
        { "OLIVE", TB_OLIVE },   { "NAVY", TB_NAVY },     { "PURPLE", TB_PURPLE },
        { "TEAL", TB_TEAL },     { "SILVER", TB_SILVER }, { "GRAY", TB_GRAY },
        { "RED", TB_RED },       { "LIME", TB_LIME },     { "YELLOW", TB_YELLOW },
        { "BLUE", TB_BLUE },     { "FUCHSIA", TB_FUCHSIA }, { "AQUA", TB_AQUA },
        { "WHITE", TB_WHITE },
    };
    for (size_t i = 0; i < sizeof colours / sizeof colours[0]; i++) {
        lua_pushinteger(L, colours[i].value);
        lua_setfield(L, -2, colours[i].name);
    }
    lua_setglobal(L, "ui");
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s script.lua\n", argv[0]);
        return 1;
    }

    L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "%s: cannot create Lua state\n", argv[0]);
        return 1;
    }
    luaL_openlibs(L);
    tl_register_ui(L);

    app = tb_app_new(TB_WIN_TITLE, TB_WIN_WIDTH, TB_WIN_HEIGHT);
    if (!app) {
        lua_close(L);
        return 1;
    }

    /* The script runs before the event loop: it builds the interface and
     * binds handlers. Handlers run later, from inside tb_run(). */
    if (luaL_dofile(L, argv[1]) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", argv[0], lua_tostring(L, -1));
        tb_app_free(app);
        lua_close(L);
        return 1;
    }

    tb_run(app);
    tb_app_free(app);
    lua_close(L);
    return 0;
}
