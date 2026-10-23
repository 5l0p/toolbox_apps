#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deps/toolbox.h/toolbox.h"

/* Sheet: a small spreadsheet on tb_grid. A cell holds a number, text, or a
 * formula starting with '='. Formulas: numbers, cell references (A1..Z100),
 * + - * / with parentheses, unary minus, and SUM, AVG, MIN, MAX over a
 * range (A1:B3) or a list of values. Files are CSV, with formulas kept as
 * their text. */

#define SH_W 520
#define SH_H 340
#define SH_STATUS_H 18
#define SH_BAR_H 22          /* formula bar strip under the menubar */
#define SH_ADDR_W 48         /* width of the cell address box        */
#define SH_ROWS 100
#define SH_COLS 26
#define SH_CW 72

enum { CV_EMPTY, CV_NUM, CV_TEXT, CV_ERR };

typedef struct {
    int kind;
    double num;
    char err[12];             /* error text, e.g. "#DIV/0!" */
} cval;

static tb_grid *grid;
static tb_status *status;
static tb_label *addr;      /* formula bar: selected cell's address */
static tb_field *bar;        /* formula bar: selected cell's text    */
static cval *cache;          /* value of each cell after the last recalc */
static unsigned char *mark;  /* 0 = not computed, 1 = in progress, 2 = done */
static char filename[1024];  /* "" = untitled */
static int dirty;

/* ---- formula evaluation ------------------------------------------------- */

typedef struct {
    const char *s;            /* next unread character */
    int err;
    char msg[12];
} P;

static cval eval_cell(int r, int c);

static void perr(P *p, const char *m) {
    if (p->err) return;
    p->err = 1;
    snprintf(p->msg, sizeof p->msg, "%s", m);
}

static void pskip(P *p) {
    while (*p->s == ' ' || *p->s == '\t') p->s++;
}

/* Reads a cell reference (letter then row number). Advances *sp past it. */
static int parse_ref(const char **sp, int *r, int *c) {
    const char *s = *sp;
    if (!isalpha((unsigned char)s[0])) return 0;
    int col = toupper((unsigned char)s[0]) - 'A';
    if (col < 0 || col >= SH_COLS) return 0;
    const char *q = s + 1;
    if (!isdigit((unsigned char)*q)) return 0;
    long row = 0;
    while (isdigit((unsigned char)*q)) {
        row = row * 10 + (*q - '0');
        if (row > SH_ROWS) return 0;
        q++;
    }
    if (row < 1) return 0;
    *r = (int)row - 1;
    *c = col;
    *sp = q;
    return 1;
}

static double pexpr(P *p);

/* Value of a referenced cell, for use in arithmetic. */
static double ref_value(P *p, int r, int c) {
    cval v = eval_cell(r, c);
    switch (v.kind) {
    case CV_EMPTY: return 0;
    case CV_NUM:   return v.num;
    case CV_TEXT:  perr(p, "#VALUE!"); return 0;
    default:       perr(p, v.err); return 0;
    }
}

typedef struct {
    double sum, min, max;
    long n;
} acc;

static void acc_add(acc *a, double v) {
    if (a->n == 0 || v < a->min) a->min = v;
    if (a->n == 0 || v > a->max) a->max = v;
    a->sum += v;
    a->n++;
}

/* A function call. q points at the '(' after the name, which ends at name_end. */
static double pfunc(P *p, const char *name_end) {
    char name[8];
    size_t len = (size_t)(name_end - p->s);
    if (len >= sizeof name) { perr(p, "#NAME?"); return 0; }
    for (size_t i = 0; i < len; i++) name[i] = (char)toupper((unsigned char)p->s[i]);
    name[len] = 0;
    int fn = !strcmp(name, "SUM") ? 0 : !strcmp(name, "AVG") ? 1 :
             !strcmp(name, "MIN") ? 2 : !strcmp(name, "MAX") ? 3 : -1;
    if (fn < 0) { perr(p, "#NAME?"); return 0; }
    p->s = name_end + 1;            /* past the '(' */

    acc a = { 0, 0, 0, 0 };
    pskip(p);
    if (*p->s == ')') {
        p->s++;
    } else {
        for (;;) {
            pskip(p);
            const char *save = p->s;
            int r1, c1, r2, c2;
            if (parse_ref(&p->s, &r1, &c1)) {
                pskip(p);
                if (*p->s == ':') {
                    p->s++;
                    pskip(p);
                    if (!parse_ref(&p->s, &r2, &c2)) { perr(p, "#SYNTAX"); return 0; }
                    int ra = r1 < r2 ? r1 : r2, rb = r1 < r2 ? r2 : r1;
                    int ca = c1 < c2 ? c1 : c2, cb = c1 < c2 ? c2 : c1;
                    for (int r = ra; r <= rb; r++)
                        for (int c = ca; c <= cb; c++) {
                            cval v = eval_cell(r, c);
                            if (v.kind == CV_ERR) { perr(p, v.err); return 0; }
                            if (v.kind == CV_NUM) acc_add(&a, v.num);
                        }
                    goto next;
                }
                p->s = save;        /* a plain reference: parse as an expression */
            }
            {
                double v = pexpr(p);
                if (p->err) return 0;
                acc_add(&a, v);
            }
        next:
            pskip(p);
            if (*p->s == ',') { p->s++; continue; }
            if (*p->s == ')') { p->s++; break; }
            perr(p, "#SYNTAX");
            return 0;
        }
    }

    switch (fn) {
    case 0: return a.sum;
    case 1:
        if (a.n == 0) { perr(p, "#DIV/0!"); return 0; }
        return a.sum / (double)a.n;
    case 2: return a.n ? a.min : 0;
    default: return a.n ? a.max : 0;
    }
}

static double pfactor(P *p) {
    pskip(p);
    if (p->err) return 0;
    if (*p->s == '(') {
        p->s++;
        double v = pexpr(p);
        pskip(p);
        if (*p->s == ')') p->s++;
        else perr(p, "#SYNTAX");
        return v;
    }
    if (*p->s == '-') { p->s++; return -pfactor(p); }
    if (*p->s == '+') { p->s++; return pfactor(p); }
    if (isdigit((unsigned char)*p->s) || *p->s == '.') {
        char *end;
        double v = strtod(p->s, &end);
        if (end == p->s) { perr(p, "#SYNTAX"); return 0; }
        p->s = end;
        return v;
    }
    if (isalpha((unsigned char)*p->s)) {
        const char *q = p->s;
        while (isalpha((unsigned char)*q)) q++;
        if (*q == '(') return pfunc(p, q);
        int r, c;
        if (parse_ref(&p->s, &r, &c)) return ref_value(p, r, c);
        perr(p, "#NAME?");
        return 0;
    }
    perr(p, "#SYNTAX");
    return 0;
}

static double pterm(P *p) {
    double v = pfactor(p);
    for (;;) {
        pskip(p);
        if (p->err) return 0;
        if (*p->s == '*') {
            p->s++;
            v *= pfactor(p);
        } else if (*p->s == '/') {
            p->s++;
            double d = pfactor(p);
            if (p->err) return 0;
            if (d == 0) { perr(p, "#DIV/0!"); return 0; }
            v /= d;
        } else {
            return v;
        }
    }
}

static double pexpr(P *p) {
    double v = pterm(p);
    for (;;) {
        pskip(p);
        if (p->err) return 0;
        if (*p->s == '+') {
            p->s++;
            v += pterm(p);
        } else if (*p->s == '-') {
            p->s++;
            v -= pterm(p);
        } else {
            return v;
        }
    }
}

/* Value of a cell. Computed once per recalc; a cell that refers back to
 * itself through other cells gets #CYCLE!. */
static cval eval_cell(int r, int c) {
    int idx = r * SH_COLS + c;
    if (mark[idx] == 2) return cache[idx];
    if (mark[idx] == 1) {
        cval e = { CV_ERR, 0, "#CYCLE!" };
        return e;
    }
    mark[idx] = 1;

    const char *raw = tb_grid_get(grid, r, c);
    cval v = { CV_EMPTY, 0, "" };
    if (raw[0] == '=') {
        P p = { raw + 1, 0, "" };
        double x = pexpr(&p);
        pskip(&p);
        if (!p.err && *p.s) perr(&p, "#SYNTAX");
        if (p.err) {
            v.kind = CV_ERR;
            snprintf(v.err, sizeof v.err, "%s", p.msg);
        } else {
            v.kind = CV_NUM;
            v.num = x;
        }
    } else if (raw[0]) {
        char *end;
        double x = strtod(raw, &end);
        while (*end == ' ') end++;
        if (end != raw && *end == 0) {
            v.kind = CV_NUM;
            v.num = x;
        } else {
            v.kind = CV_TEXT;
        }
    }
    cache[idx] = v;
    mark[idx] = 2;
    return v;
}

static void recalc(void) {
    memset(mark, 0, (size_t)SH_ROWS * SH_COLS);
    for (int r = 0; r < SH_ROWS; r++)
        for (int c = 0; c < SH_COLS; c++)
            eval_cell(r, c);
}

/* What the grid draws in a cell: the value of a formula, the number or text
 * otherwise. */
static const char *cell_shown(tb_grid *g, int r, int c, void *data) {
    (void)g; (void)data;
    static char out[32];
    cval v = eval_cell(r, c);
    switch (v.kind) {
    case CV_NUM:
        snprintf(out, sizeof out, "%.10g", v.num);
        return out;
    case CV_TEXT:
        return tb_grid_get(grid, r, c);
    case CV_ERR:
        snprintf(out, sizeof out, "%s", v.err);
        return out;
    default:
        return "";
    }
}

/* ---- status, title and changes ------------------------------------------ */

/* File name without its directory, or "untitled.csv". */
static const char *file_base(void) {
    if (!filename[0]) return "untitled.csv";
    const char *slash = strrchr(filename, '/');
    return slash ? slash + 1 : filename;
}

/* The formula bar shows the selected cell's text, or the text being typed
 * into the cell. */
static void bar_refresh(tb_app *app) {
    int r = grid->sr, c = grid->sc;
    char cell[16];
    snprintf(cell, sizeof cell, "%c%d", 'A' + c, r + 1);
    tb_label_set(app, addr, cell);
    tb_field_set(bar, grid->editing ? grid->edit : tb_grid_get(grid, r, c));
}

static void refresh(tb_app *app) {
    char buf[1100], title[1200];
    int r = grid->sr, c = grid->sc;
    const char *raw = grid->editing ? grid->edit : tb_grid_get(grid, r, c);
    snprintf(buf, sizeof buf, "%c%d   %s   %s", 'A' + c, r + 1, raw,
             grid->editing ? "" : cell_shown(grid, r, c, NULL));
    tb_status_set(status, buf);
    bar_refresh(app);

    snprintf(title, sizeof title, "%s%s - Sheet", file_base(), dirty ? " (edited)" : "");
    tb_set_title(app, title);
}

static void on_select(tb_app *app, void *data) {
    (void)data;
    refresh(app);
}

static void on_change(tb_app *app, void *data) {
    (void)data;
    dirty = 1;
    recalc();
    refresh(app);
}

/* Typing in a cell: mirror the text in the formula bar. */
static void on_edit(tb_app *app, void *data) {
    (void)data;
    bar_refresh(app);
}

/* Return in the formula bar: store its text in the selected cell. */
static void on_bar_accept(tb_app *app, void *data) {
    (void)data;
    grid->editing = 0;
    tb_grid_set(grid, grid->sr, grid->sc, tb_field_get(bar));
    dirty = 1;
    recalc();
    refresh(app);
    app->root.focus = &grid->base;
    tb_damage_all(app);
}

/* Before losing the sheet: 1 = go ahead, 0 = cancel. */
static int confirm_discard(tb_app *app) {
    if (!dirty) return 1;
    int r = tb_alert(app, TB_ALERT_CAUTION, "Discard the changes to this sheet?",
                     "Discard", "Cancel", NULL);
    return r == 1;
}

/* ---- CSV files ---------------------------------------------------------- */

static void csv_field(FILE *f, const char *s) {
    if (!strpbrk(s, ",\"\n\r")) { fputs(s, f); return; }
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static int save_csv(tb_app *app, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        tb_alert(app, TB_ALERT_STOP, "Could not save the file.", "OK", NULL, NULL);
        return 0;
    }
    for (int r = 0; r < SH_ROWS; r++) {
        int last = -1;
        for (int c = 0; c < SH_COLS; c++)
            if (*tb_grid_get(grid, r, c)) last = c;
        for (int c = 0; c <= last; c++) {
            if (c) fputc(',', f);
            csv_field(f, tb_grid_get(grid, r, c));
        }
        if (last >= 0) fputc('\n', f);
    }
    int ok = fclose(f) == 0;
    if (!ok) tb_alert(app, TB_ALERT_STOP, "Could not save the file.", "OK", NULL, NULL);
    return ok;
}

/* Fills the grid from a CSV file. Returns 0 if the file cannot be read. */
static int load_csv(tb_app *app, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        tb_alert(app, TB_ALERT_STOP, "Could not open that file.", "OK", NULL, NULL);
        return 0;
    }
    tb_grid_clear(grid);
    char field[1024];
    size_t n = 0;
    int r = 0, c = 0, quoted = 0, ch;
    int in_field = 0;
    for (;;) {
        ch = fgetc(f);
        if (quoted) {
            if (ch == EOF) break;
            if (ch == '"') {
                int nx = fgetc(f);
                if (nx == '"') { if (n < sizeof field - 1) field[n++] = '"'; }
                else { quoted = 0; if (nx != EOF) ungetc(nx, f); }
            } else if (n < sizeof field - 1) {
                field[n++] = (char)ch;
            }
            continue;
        }
        if (ch == EOF || ch == ',' || ch == '\n' || ch == '\r') {
            field[n] = 0;
            if (r < SH_ROWS && c < SH_COLS && (in_field || n)) tb_grid_set(grid, r, c, field);
            n = 0;
            in_field = 0;
            if (ch == ',') { c++; continue; }
            if (ch == '\r') { int nx = fgetc(f); if (nx != '\n' && nx != EOF) ungetc(nx, f); }
            if (ch == EOF) break;
            r++;
            c = 0;
            continue;
        }
        in_field = 1;
        if (ch == '"' && n == 0) quoted = 1;
        else if (n < sizeof field - 1) field[n++] = (char)ch;
    }
    fclose(f);
    recalc();
    dirty = 0;
    snprintf(filename, sizeof filename, "%s", path);
    refresh(app);
    tb_damage_all(app);
    return 1;
}

/* ---- File menu ---------------------------------------------------------- */

static void file_new(tb_app *app, void *data) {
    (void)data;
    if (!confirm_discard(app)) return;
    tb_grid_clear(grid);
    filename[0] = 0;
    dirty = 0;
    recalc();
    refresh(app);
}

static void file_open(tb_app *app, void *data) {
    char path[1024];
    (void)data;
    if (!confirm_discard(app)) return;
    if (tb_open_dialog(app, ".", path, sizeof path)) load_csv(app, path);
}

static int save_as(tb_app *app) {
    char path[1024];
    if (!tb_save_dialog(app, ".", file_base(), path, sizeof path))
        return 0;
    if (!save_csv(app, path)) return 0;
    snprintf(filename, sizeof filename, "%s", path);
    dirty = 0;
    refresh(app);
    return 1;
}

static void file_save(tb_app *app, void *data) {
    (void)data;
    if (!filename[0]) { save_as(app); return; }
    if (save_csv(app, filename)) {
        dirty = 0;
        refresh(app);
    }
}

static void file_save_as(tb_app *app, void *data) {
    (void)data;
    save_as(app);
}

static void file_quit(tb_app *app, void *data) {
    (void)data;
    if (confirm_discard(app)) tb_quit(app);
}

static int on_close(tb_app *app, void *data) {
    (void)data;
    return confirm_discard(app);
}

/* ---- main --------------------------------------------------------------- */

static void layout(tb_app *app) {
    int top = TB_MENUBAR_H;
    addr->base.x = 6;
    addr->base.y = top + (SH_BAR_H - (app->fascent + app->fdescent)) / 2;
    bar->base.x = SH_ADDR_W;
    bar->base.y = top + 2;
    bar->base.w = app->width - SH_ADDR_W - 4;
    bar->base.h = SH_BAR_H - 4;
    grid->base.x = 0;
    grid->base.y = top + SH_BAR_H;
    grid->base.w = app->width;
    grid->base.h = app->height - top - SH_BAR_H - SH_STATUS_H;
    status->base.x = 0;
    status->base.y = app->height - SH_STATUS_H;
    status->base.w = app->width;
    tb_damage_all(app);
}

static void on_resize(tb_app *app, void *data) {
    (void)data;
    layout(app);
}

int main(void) {
    tb_app *app = tb_app_new("Sheet", SH_W, SH_H);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "New", file_new, NULL);
    tb_menu_item(app, "Open...", file_open, NULL);
    tb_menu_item(app, "Save", file_save, NULL);
    tb_menu_item(app, "Save As...", file_save_as, NULL);
    tb_menu_item(app, "-", NULL, NULL);
    tb_menu_item(app, "Quit", file_quit, NULL);

    addr = tb_label_new(app, 6, 0, TB_C_FG, "A1");
    bar = tb_field_new(app, SH_ADDR_W, 0, SH_W - SH_ADDR_W - 4, SH_BAR_H - 4);
    tb_field_on_accept(bar, on_bar_accept, NULL);

    grid = tb_grid_new(app, 0, 0, SH_W, 0, SH_ROWS, SH_COLS, SH_CW);
    tb_grid_on_show(grid, cell_shown, NULL);
    tb_grid_on_select(grid, on_select, NULL);
    tb_grid_on_change(grid, on_change, NULL);
    tb_grid_on_edit(grid, on_edit, NULL);
    status = tb_status_new(app, 0, SH_H - SH_STATUS_H, SH_W, SH_STATUS_H, "");

    cache = calloc((size_t)SH_ROWS * SH_COLS, sizeof *cache);
    mark = calloc((size_t)SH_ROWS * SH_COLS, 1);
    if (!cache || !mark) tb_oom();

    app->root.focus = &grid->base;
    tb_on_close(app, on_close, NULL);
    tb_on_resize(app, on_resize, NULL);
    layout(app);
    refresh(app);

    tb_run(app);
    tb_app_free(app);
    free(cache);
    free(mark);
    return 0;
}
