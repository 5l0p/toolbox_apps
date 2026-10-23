#ifndef CONFIG_H
#define CONFIG_H

/* ---- Window ------------------------------------------------------------ */
#define TB_WIN_TITLE     "Toolbox"
#define TB_WIN_WIDTH     480
#define TB_WIN_HEIGHT    320
#define TB_MIN_W         120  /* smallest size a window can be resized to */
#define TB_MIN_H         80

/* ---- Fonts (core X11 font names; first one that loads is used) --------- */
#define TB_FONT          "-misc-fixed-medium-r-normal--13-*-*-*-*-*-iso8859-1"
#define TB_FONT_FALLBACK "fixed"

/* ---- Layout (pixels) --------------------------------------------------- */
#define TB_MENUBAR_H        20   /* height of the menubar strip at the top  */
#define TB_MENU_ITEM_H      18   /* height of one dropdown item             */
#define TB_MENU_PAD_X        8   /* horizontal padding for titles and items */
#define TB_MENU_MIN_W      120   /* minimum dropdown width                  */
#define TB_BUTTON_BORDER     2   /* button outline thickness                */
#define TB_SCROLL_W         16   /* scrollbar thickness (and arrow box size) */
#define TB_SCROLL_MIN_THUMB  8   /* smallest thumb length                    */
#define TB_SCROLL_STEP      16   /* horizontal text scroll step, in pixels   */
#define TB_TEXT_PAD          2   /* inner padding of text widgets            */
#define TB_ROW_PAD           2   /* vertical padding inside list/tree rows   */
#define TB_CELL_PAD          4   /* horizontal padding inside list cells     */
#define TB_TREE_INDENT      14   /* indent per tree level                    */
#define TB_TREE_DISC        12   /* size of the expand/collapse triangle     */
#define TB_DBLCLICK_MS     400   /* double-click interval                    */

/* ---- VS Code Dark+ inspired palette ----------------------------------- */
enum {
    TB_BLACK,    /*  0: editor background */
    TB_MAROON,   /*  1: dark red */
    TB_GREEN,    /*  2: dark green */
    TB_OLIVE,    /*  3: dark yellow */
    TB_NAVY,     /*  4: dark blue */
    TB_PURPLE,   /*  5: dark purple */
    TB_TEAL,     /*  6: dark cyan */
    TB_SILVER,   /*  7: light gray */
    TB_GRAY,     /*  8: muted gray */
    TB_RED,      /*  9: red */
    TB_LIME,     /* 10: green */
    TB_YELLOW,   /* 11: yellow */
    TB_BLUE,     /* 12: blue */
    TB_FUCHSIA,  /* 13: pink */
    TB_AQUA,     /* 14: cyan */
    TB_WHITE,    /* 15: foreground */
    TB_ORANGE    /* 16: status bar */
};

static const char *const tb_palette[] = {
    "#1e1e1e", "#5a1d1d", "#1e4d2b", "#665c00",
    "#1f3a5f", "#4b2c5e", "#1b4d4f", "#d4d4d4",
    "#808080", "#f44747", "#6a9955", "#dcdcaa",
    "#569cd6", "#c586c0", "#4ec9b0", "#d4d4d4",
    "#d7822a"
};

/* ---- Semantic roles (VS Code Dark+ inspired) --------------------------- */
#define TB_C_BG          TB_WHITE  /* window and menubar background */
#define TB_C_FG          TB_BLACK  /* text, borders, lines          */
#define TB_C_SEL_BG      TB_NAVY   /* selected menu title / item    */
#define TB_C_SEL_FG      TB_WHITE
#define TB_C_MENUBAR     TB_ORANGE    /* menubar strip                */
#define TB_C_STATUS_BG   TB_ORANGE   /* status bar background         */
#define TB_C_STATUS_FG   TB_BLACK    /* status bar text              */
#define TB_C_SCROLL_THUMB TB_AQUA    /* scroll bar thumb             */


#endif /* CONFIG_H */
