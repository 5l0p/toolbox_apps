#ifndef TELETYPE_H
#define TELETYPE_H

/*
 * teletype.h - a dumb terminal panel. Lines typed at the prompt go to one
 * long-lived /bin/sh; its stdout and stderr stream into the transcript above
 * the prompt. There is no cursor addressing: control bytes are dropped.
 *
 * Used by teletype.c (the standalone app) and as a fixed tab in codeedit.c.
 * The caller positions the panel with tt_panel_place() and shows or hides it
 * with tt_panel_show(). Call tt_panel_start() once to start the shell.
 */


#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "deps/toolbox.h/toolbox.h"

#define TT_PROMPT_H 24   /* strip at the bottom of the panel holding the prompt */

typedef struct tt_panel {
    tb_app *app;
    tb_textview *screen;      /* transcript                              */
    tb_field *input;          /* the prompt's input line                 */
    tb_label *prompt;         /* the "$" before the input line           */
    int to_sh;                /* write end of the shell's stdin, -1 = none */
    int from_sh;              /* read end of the shell's stdout + stderr  */
    pid_t sh_pid;
    char *last_cmd;           /* for !!                                  */
    tb_action on_exit;        /* called when the shell exits, or NULL    */
    void *exit_data;
} tt_panel;

/* ---- output ------------------------------------------------------------- */

/* Keep printable ASCII and newlines. Tabs become a space, UTF-8 bytes a '?',
 * and other control bytes (CR, ESC, BEL...) are dropped. Returns the length. */
static inline int tt_clean(const char *in, int n, char *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\n') out[m++] = '\n';
        else if (c == '\t') out[m++] = ' ';
        else if (c >= 127) out[m++] = '?';
        else if (c >= 32) out[m++] = (char)c;
    }
    out[m] = 0;
    return m;
}

/* Runs from the event loop whenever the shell has written something. */
static inline void tt_on_output(tb_app *app, int fd, void *data) {
    tt_panel *p = data;
    char raw[512], out[sizeof raw + 1];
    ssize_t n = read(fd, raw, sizeof raw);
    if (n > 0) {
        tt_clean(raw, (int)n, out);
        tb_textview_append(app, p->screen, out);
        return;
    }
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;

    /* EOF or error: the shell has exited. A new command starts a new one. */
    tb_unwatch_fd(app, fd);
    close(fd);
    p->from_sh = -1;
    if (p->to_sh >= 0) close(p->to_sh);
    p->to_sh = -1;
    if (p->sh_pid > 0) waitpid(p->sh_pid, NULL, WNOHANG);
    p->sh_pid = -1;
    tb_textview_append(app, p->screen, "[shell exited]\n");
    if (p->on_exit) p->on_exit(app, p->exit_data);
}

/* ---- shell -------------------------------------------------------------- */

static inline int tt_start_shell(tt_panel *p) {
    int in[2], out[2];
    if (pipe(in) < 0) return 0;
    if (pipe(out) < 0) {
        close(in[0]);
        close(in[1]);
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(in[0]); close(in[1]);
        close(out[0]); close(out[1]);
        return 0;
    }
    if (pid == 0) {                       /* child: become the shell */
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        close(in[0]); close(in[1]);
        close(out[0]); close(out[1]);
        setenv("TERM", "dumb", 1);
        setenv("EDITOR", "busybox ed", 1);
        setenv("NOCOLOR", "1", 1);
        execl("/bin/sh", "sh", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    p->to_sh = in[1];
    p->from_sh = out[0];
    p->sh_pid = pid;
    tb_watch_fd(p->app, p->from_sh, tt_on_output, p);
    return 1;
}

static inline int tt_write_all(int fd, const char *s, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        s += w;
        n -= (size_t)w;
    }
    return 1;
}

/* ---- prompt ------------------------------------------------------------- */

/* Expands !! to the last command. Returns a new string the caller frees, or
 * NULL on allocation failure. */
static inline char *tt_expand_bang_bang(const tt_panel *p, const char *cmd) {
    if (!p->last_cmd || !strstr(cmd, "!!")) {
        char *copy = malloc(strlen(cmd) + 1);
        if (copy) strcpy(copy, cmd);
        return copy;
    }

    size_t cmd_len = strlen(cmd);
    size_t last_len = strlen(p->last_cmd);
    size_t count = 0;
    for (const char *s = cmd; (s = strstr(s, "!!")) != NULL; s += 2) count++;
    char *expanded = malloc(cmd_len + count * last_len + 1);
    if (!expanded) return NULL;

    const char *src = cmd;
    char *dst = expanded;
    const char *bang;
    while ((bang = strstr(src, "!!")) != NULL) {
        size_t chunk = (size_t)(bang - src);
        memcpy(dst, src, chunk);
        dst += chunk;
        memcpy(dst, p->last_cmd, last_len);
        dst += last_len;
        src = bang + 2;
    }
    strcpy(dst, src);
    return expanded;
}

static inline void tt_on_enter(tb_app *app, void *data) {
    tt_panel *p = data;
    const char *cmd = tb_field_get(p->input);

    /* "clear" is handled here: wipe the transcript, don't send it to sh. */
    if (strcmp(cmd, "clear") == 0) {
        tb_textview_set(p->screen, "");
        tb_field_set(p->input, "");
        return;
    }

    char *expanded = tt_expand_bang_bang(p, cmd);
    if (!expanded) return;

    size_t len = strlen(expanded);
    char *line = malloc(len + 2);
    if (!line) {
        free(expanded);
        return;
    }
    memcpy(line, expanded, len);
    line[len] = '\n';
    line[len + 1] = 0;

    /* Show the command (after expansion, so !! is visible). */
    tb_textview_append(app, p->screen, line);
    tb_field_set(p->input, "");

    if (strcmp(expanded, "!!") != 0) {
        free(p->last_cmd);
        p->last_cmd = malloc(len + 1);
        if (p->last_cmd) strcpy(p->last_cmd, expanded);
    }

    if (p->to_sh < 0 && !tt_start_shell(p)) {
        tb_textview_append(app, p->screen, "[cannot start /bin/sh]\n");
    } else if (!tt_write_all(p->to_sh, line, len + 1)) {
        tb_textview_append(app, p->screen, "[write to shell failed]\n");
    }
    free(expanded);
    free(line);
}

/* ---- panel -------------------------------------------------------------- */

/* Create the panel's widgets. They start hidden-or-not as the app's default;
 * call tt_panel_place() before drawing. */
static inline tt_panel *tt_panel_new(tb_app *app) {
    tt_panel *p = calloc(1, sizeof *p);
    if (!p) tb_oom();
    p->app = app;
    p->to_sh = -1;
    p->from_sh = -1;
    p->sh_pid = -1;
    p->screen = tb_textview_new(app, 0, 0, 1, 1);
    tb_textview_set_wrap(app, p->screen, 1);   /* wrapped lines, no horizontal bar */
    p->prompt = tb_label_new(app, 0, 0, TB_C_FG, "$");
    p->input = tb_field_new(app, 0, 0, 1, 0);
    p->input->plain = 1;
    tb_field_on_accept(p->input, tt_on_enter, p);
    return p;
}

/* Put the panel in rectangle (x, y, w, h): transcript above, prompt below. */
static inline void tt_panel_place(tb_app *app, tt_panel *p, int x, int y, int w, int h) {
    int py = y + h - TT_PROMPT_H;
    int fh = app->fascent + app->fdescent;
    tb_textview_resize(app, p->screen, x, y, w, h - TT_PROMPT_H);
    p->prompt->base.x = x + 6;
    p->prompt->base.y = py + (TT_PROMPT_H - fh) / 2;
    p->input->base.x = x + 18;
    p->input->base.y = py + 2;
    p->input->base.w = w - 24 > 1 ? w - 24 : 1;
    p->input->base.h = TT_PROMPT_H - 4;
    tb_damage_all(app);
}

/* Show or hide the whole panel (used by tabs). */
static inline void tt_panel_show(tb_app *app, tt_panel *p, int on) {
    tb_widget_hide(&p->screen->base, !on);
    tb_widget_hide(&p->prompt->base, !on);
    tb_widget_hide(&p->input->base, !on);
    (void)app;
}

/* Start the shell and print the banner. */
static inline void tt_panel_start(tb_app *app, tt_panel *p) {
    tt_start_shell(p);
    tb_textview_append(app, p->screen, "Teletype. Type a command and press Return.\n");
}

/* Stop the shell: ask it to exit, reap it, close the pipes. */
static inline void tt_panel_stop(tt_panel *p) {
    if (p->sh_pid > 0) {
        kill(p->sh_pid, SIGTERM);
        waitpid(p->sh_pid, NULL, 0);
        p->sh_pid = -1;
    }
    if (p->from_sh >= 0) {
        tb_unwatch_fd(p->app, p->from_sh);
        close(p->from_sh);
        p->from_sh = -1;
    }
    if (p->to_sh >= 0) {
        close(p->to_sh);
        p->to_sh = -1;
    }
}

#endif /* TELETYPE_H */
