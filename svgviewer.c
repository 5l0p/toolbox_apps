#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "deps/toolbox.h/toolbox.h"

#define NANOSVG_IMPLEMENTATION
#include "deps/nanosvg/src/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "deps/nanosvg/src/nanosvgrast.h"

/* SVG viewer: opens an SVG with nanosvg, rasterizes it with nanosvgrast
 * into an RGBA buffer sized to the view, and blits it with XPutImage.
 * Zoom with the slider or Fit; drag inside the view to pan. */

#define WIN_W 640
#define WIN_H 480
#define VIEW_X 8
#define VIEW_Y 56
#define VIEW_PAD_W 16          /* horizontal space taken by the margins */
#define VIEW_PAD_H 88          /* vertical space taken by toolbar + status */
#define STATUS_H 18

/* Pixel layout of the X visual, detected from the window's default visual. */
static struct {
    int bpp;                   /* bits per pixel of the XImage */
    int msb;                   /* 1 if the XImage is most-significant-byte first */
    int rs, rb, gs, gb, bs, bb;/* shift and width of each channel */
} fmt;

typedef struct {
    tb_widget base;
    NSVGimage *img;
    NSVGrasterizer *rast;
    unsigned char *rgba;      /* scratch RGBA buffer, view-sized */
    size_t rgba_cap;
    XImage *xi;               /* view-sized image handed to the X server */
    int dirty;                /* the XImage must be re-rendered */
    int fit;                  /* 1 = scale to fit the view, 0 = use zoom */
    int zoom;                 /* percent of the natural size (96 dpi) */
    int panx, pany;           /* pan offset from the centred position */
    int drag, dragx, dragy, dragpx, dragpy;
} svview;

static svview *view;
static tb_slider *zoomsl;
static tb_status *status;
static char path[4096];

static void chan_info(unsigned long mask, int *shift, int *bits) {
    *shift = 0;
    *bits = 0;
    if (!mask) return;
    while (!(mask & 1)) { mask >>= 1; (*shift)++; }
    while (mask & 1) { mask >>= 1; (*bits)++; }
}

/* Scale an 8-bit channel to `bits` bits, then move it to its position. */
static unsigned long chan(unsigned c, int bits) {
    unsigned long max = (1ul << bits) - 1;
    return ((unsigned long)c * max + 127) / 255;
}

static void make_xi(tb_app *t, svview *v, int w, int h) {
    Visual *vis = DefaultVisual(t->dpy, t->screen);
    if (v->xi) XDestroyImage(v->xi);   /* also frees the old data */
    v->xi = XCreateImage(t->dpy, vis, (unsigned)DefaultDepth(t->dpy, t->screen),
                         ZPixmap, 0, NULL, (unsigned)w, (unsigned)h, 32, 0);
    if (!v->xi) tb_oom();
    v->xi->data = malloc((size_t)v->xi->bytes_per_line * (size_t)h);
    if (!v->xi->data) tb_oom();
    fmt.bpp = v->xi->bits_per_pixel;
    fmt.msb = v->xi->byte_order == MSBFirst;
    chan_info(vis->red_mask, &fmt.rs, &fmt.rb);
    chan_info(vis->green_mask, &fmt.gs, &fmt.gb);
    chan_info(vis->blue_mask, &fmt.bs, &fmt.bb);
}

/* Rasterize the image for the current zoom and pan into the XImage,
 * compositing over white. */
static void render(tb_app *t, svview *v, tb_widget *wb) {
    int vw = wb->w, vh = wb->h;
    if (!v->xi || v->xi->width != vw || v->xi->height != vh)
        make_xi(t, v, vw, vh);

    size_t need = (size_t)vw * (size_t)vh * 4;
    if (need > v->rgba_cap) {
        free(v->rgba);
        v->rgba = malloc(need);
        if (!v->rgba) tb_oom();
        v->rgba_cap = need;
    }

    float iw0 = v->img->width > 0 ? v->img->width : 1.0f;
    float ih0 = v->img->height > 0 ? v->img->height : 1.0f;
    float s = v->fit ? fminf(vw / iw0, vh / ih0) : v->zoom / 100.0f;
    if (s <= 0.0f) s = 0.01f;
    float iw = iw0 * s, ih = ih0 * s;
    float cx = (vw - iw) / 2.0f, cy = (vh - ih) / 2.0f;
    float tx = cx, ty = cy;

    /* Pan is only possible along an axis where the image is larger than
     * the view; otherwise the image stays centred. */
    if (iw > vw) {
        float px = cx + v->panx;
        if (px > 0) px = 0;
        if (px < vw - iw) px = vw - iw;
        tx = px;
        v->panx = (int)lrintf(px - cx);
    } else {
        v->panx = 0;
    }
    if (ih > vh) {
        float py = cy + v->pany;
        if (py > 0) py = 0;
        if (py < vh - ih) py = vh - ih;
        ty = py;
        v->pany = (int)lrintf(py - cy);
    } else {
        v->pany = 0;
    }

    memset(v->rgba, 0, need);
    nsvgRasterize(v->rast, v->img, tx, ty, s, v->rgba, vw, vh, vw * 4);

    int bytes = fmt.bpp / 8;
    for (int y = 0; y < vh; y++) {
        const unsigned char *src = v->rgba + (size_t)y * vw * 4;
        unsigned char *dst = (unsigned char *)v->xi->data + (size_t)y * v->xi->bytes_per_line;
        for (int x = 0; x < vw; x++, src += 4, dst += bytes) {
            unsigned a = src[3];
            unsigned r = (src[0] * a + 255 * (255 - a) + 127) / 255;
            unsigned g = (src[1] * a + 255 * (255 - a) + 127) / 255;
            unsigned b = (src[2] * a + 255 * (255 - a) + 127) / 255;
            unsigned long pix = (chan(r, fmt.rb) << fmt.rs) |
                                (chan(g, fmt.gb) << fmt.gs) |
                                (chan(b, fmt.bb) << fmt.bs);
            for (int i = 0; i < bytes; i++) {
                int idx = fmt.msb ? bytes - 1 - i : i;
                dst[idx] = (unsigned char)((pix >> (8 * i)) & 0xff);
            }
        }
    }
    v->dirty = 0;
}

static void sv_draw(tb_app *t, tb_widget *wb, int focused) {
    svview *v = (svview *)wb;
    (void)focused;
    if (wb->w < 1 || wb->h < 1) return;
    tb_fill(t, wb->x, wb->y, wb->w, wb->h, TB_C_BG);
    if (!v->img) {
        tb_str(t, wb->x + 10, wb->y + 20, TB_C_FG, "Use File > Open... to load an SVG.");
    } else {
        if (v->dirty || !v->xi || v->xi->width != wb->w || v->xi->height != wb->h)
            render(t, v, wb);
        XPutImage(t->dpy, t->buf, t->gc, v->xi, 0, 0,
                  wb->x + t->ox, wb->y + t->oy, (unsigned)wb->w, (unsigned)wb->h);
    }
    tb_frame(t, wb->x, wb->y, wb->w, wb->h, TB_C_FG);
}

static int sv_press(tb_app *t, tb_widget *wb, int x, int y) {
    (void)t;
    svview *v = (svview *)wb;
    if (!v->img) return 0;
    v->drag = 1;
    v->dragx = x;
    v->dragy = y;
    v->dragpx = v->panx;
    v->dragpy = v->pany;
    return 1;
}

static void sv_motion(tb_app *t, tb_widget *wb, int x, int y) {
    (void)t;
    svview *v = (svview *)wb;
    if (!v->drag) return;
    v->panx = v->dragpx + (x - v->dragx);
    v->pany = v->dragpy + (y - v->dragy);
    v->dirty = 1;
}

static void sv_release(tb_app *t, tb_widget *wb, int x, int y) {
    (void)t;
    (void)x;
    (void)y;
    ((svview *)wb)->drag = 0;
}

static void sv_destroy(tb_widget *wb) {
    svview *v = (svview *)wb;
    if (v->img) nsvgDelete(v->img);
    if (v->rast) nsvgDeleteRasterizer(v->rast);
    if (v->xi) XDestroyImage(v->xi);
    free(v->rgba);
    free(v);
}

static const tb_wops SVVIEW_OPS = {
    sv_draw, sv_press, sv_motion, sv_release, NULL, NULL, sv_destroy, NULL
};

static svview *sv_new(tb_app *t, int x, int y, int w, int h) {
    svview *v = calloc(1, sizeof *v);
    if (!v) tb_oom();
    v->base.ops = &SVVIEW_OPS;
    v->base.x = x;
    v->base.y = y;
    v->base.w = w;
    v->base.h = h;
    v->base.clickable = 1;
    v->fit = 1;
    v->zoom = 100;
    v->rast = nsvgCreateRasterizer();
    if (!v->rast) tb_oom();
    tb_add_widget(t, &v->base);
    return v;
}

static void update_status(void) {
    char buf[4200];
    if (!view->img) {
        tb_status_set(status, "No file open");
        return;
    }
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (view->fit)
        snprintf(buf, sizeof buf, "%s   %.0f x %.0f   Fit", name,
                 view->img->width, view->img->height);
    else
        snprintf(buf, sizeof buf, "%s   %.0f x %.0f   %d%%", name,
                 view->img->width, view->img->height, view->zoom);
    tb_status_set(status, buf);
}

static void sv_load(tb_app *app, const char *file) {
    NSVGimage *img = nsvgParseFromFile(file, "px", 96.0f);
    if (!img) {
        char msg[4200];
        snprintf(msg, sizeof msg, "Cannot open or parse \"%s\".", file);
        tb_alert(app, TB_ALERT_CAUTION, msg, "OK", NULL, NULL);
        return;
    }
    if (view->img) nsvgDelete(view->img);
    view->img = img;
    snprintf(path, sizeof path, "%s", file);
    view->fit = 1;
    view->panx = 0;
    view->pany = 0;
    view->dirty = 1;
    update_status();
    tb_damage_all(app);
}

static void on_open(tb_app *app, void *data) {
    (void)data;
    char buf[4096];
    if (tb_open_dialog(app, ".", buf, sizeof buf))
        sv_load(app, buf);
}

static void on_zoom(tb_app *app, void *data) {
    (void)data;
    view->zoom = tb_slider_get(zoomsl);
    view->fit = 0;
    view->panx = 0;
    view->pany = 0;
    view->dirty = 1;
    update_status();
    tb_damage_all(app);
}

static void on_fit(tb_app *app, void *data) {
    (void)data;
    view->fit = 1;
    view->panx = 0;
    view->pany = 0;
    view->dirty = 1;
    update_status();
    tb_damage_all(app);
}

static void on_quit(tb_app *app, void *data) {
    (void)data;
    tb_quit(app);
}

/* Keep the widgets sized to the window. */
static void on_resize(tb_app *app, void *data) {
    (void)data;
    int W = app->width, H = app->height;
    view->base.w = W - VIEW_PAD_W;
    view->base.h = H - VIEW_PAD_H;
    view->dirty = 1;
    status->base.y = H - STATUS_H - 8;
    status->base.w = W - VIEW_PAD_W;
}

int main(int argc, char **argv) {
    tb_app *app = tb_app_new("SVG Viewer", WIN_W, WIN_H);
    if (!app) return 1;

    tb_menu_new(app, "File");
    tb_menu_item(app, "Open...", on_open, NULL);
    tb_menu_item(app, "Quit", on_quit, NULL);

    tb_label_new(app, 8, 30, TB_BLACK, "Zoom:");
    zoomsl = tb_slider_new(app, 56, 26, 200, 20, 0, 10, 400, 100, on_zoom, NULL);
    zoomsl->step = 10;
    tb_slider_set(zoomsl, 100);
    tb_button_new(app, 270, 26, 60, 22, "Fit", on_fit, NULL);
    tb_button_new(app, 340, 26, 80, 22, "Open...", on_open, NULL);

    view = sv_new(app, VIEW_X, VIEW_Y,
                  WIN_W - VIEW_PAD_W, WIN_H - VIEW_PAD_H);
    status = tb_status_new(app, VIEW_X, WIN_H - STATUS_H - 8,
                           WIN_W - VIEW_PAD_W, STATUS_H, "No file open");

    tb_on_resize(app, on_resize, NULL);
    if (argc > 1) sv_load(app, argv[1]);

    tb_run(app);
    tb_app_free(app);
    return 0;
}
