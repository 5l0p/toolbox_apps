#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "deps/toolbox.h/config.h"
#include "deps/toolbox.h/toolbox.h"

/* Teletype: a dumb terminal. Lines typed at the bottom prompt go to one
 * long-lived /bin/sh; its stdout and stderr stream into the transcript
 * above. There is no cursor addressing: control bytes are dropped. */

#define TT_W 520
#define TT_H 320
#define TT_PROMPT_H 24   /* strip at the bottom holding the prompt */

static tb_textview *screen;
static tb_field *input;
static tb_label *prompt;
static int to_sh = -1;     /* write end of the shell's stdin           */
static int from_sh = -1;   /* read end of the shell's stdout + stderr  */
static pid_t sh_pid = -1;
static char *last_cmd = NULL;  /* stores the last executed command */

/* ---- output ------------------------------------------------------------- */

/* Keep printable ASCII and newlines. Tabs become a space, UTF-8 bytes a '?',
 * and other control bytes (CR, ESC, BEL...) are dropped. Returns the length. */
static int clean(const char *in, int n, char *out) {
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
static void on_output(tb_app *app, int fd, void *data) {
    (void)data;
    char raw[512], out[sizeof raw + 1];
    ssize_t n = read(fd, raw, sizeof raw);
    if (n > 0) {
        clean(raw, (int)n, out);
        tb_textview_append(app, screen, out);
        return;
    }
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;

    /* EOF or error: the shell has exited, so there is nothing left to run. */
    tb_unwatch_fd(app, fd);
    close(fd);
    from_sh = -1;
    if (to_sh >= 0) close(to_sh);
    to_sh = -1;
    waitpid(sh_pid, NULL, WNOHANG);
    sh_pid = -1;
    tb_quit(app);
}

/* ---- shell -------------------------------------------------------------- */

static int start_shell(tb_app *app) {
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
    to_sh = in[1];
    from_sh = out[0];
    sh_pid = pid;
    tb_watch_fd(app, from_sh, on_output, NULL);
    return 1;
}

static int write_all(int fd, const char *s, size_t n) {
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

/* Expands !! to the last command. Returns a new string that should be freed
 * by the caller. If !! is not found or there's no last command, returns a copy
 * of the input string. Returns NULL on allocation failure. */
static char *expand_bang_bang(const char *cmd) {
    if (!last_cmd || !strstr(cmd, "!!")) {
        char *copy = malloc(strlen(cmd) + 1);
        if (copy) strcpy(copy, cmd);
        return copy;
    }

    /* Allocate space for the expanded command */
    size_t cmd_len = strlen(cmd);
    size_t last_cmd_len = strlen(last_cmd);
    char *expanded = malloc(cmd_len + last_cmd_len);
    if (!expanded) return NULL;

    /* Replace all occurrences of !! with the last command */
    const char *src = cmd;
    char *dst = expanded;
    const char *bang_ptr;

    while ((bang_ptr = strstr(src, "!!")) != NULL) {
        /* Copy everything up to !! */
        size_t chunk_len = bang_ptr - src;
        memcpy(dst, src, chunk_len);
        dst += chunk_len;

        /* Copy the last command */
        strcpy(dst, last_cmd);
        dst += last_cmd_len;

        /* Move past the !! */
        src = bang_ptr + 2;
    }

    /* Copy the rest of the command */
    strcpy(dst, src);

    return expanded;
}

static void on_enter(tb_app *app, void *data) {
    (void)data;
    const char *cmd = tb_field_get(input);

    /* "clear" is handled here: wipe the transcript, don't send it to sh. */
    if (strcmp(cmd, "clear") == 0) {
        tb_textview_set(screen, "");
        tb_field_set(input, "");
        return;
    }

    /* Expand !! to the last command */
    char *expanded = expand_bang_bang(cmd);
    if (!expanded) return;

    size_t expanded_len = strlen(expanded);
    char *line = malloc(expanded_len + 2);
    if (!line) {
        free(expanded);
        return;
    }
    memcpy(line, expanded, expanded_len);
    line[expanded_len] = '\n';
    line[expanded_len + 1] = 0;

    /* Display the expanded command (for visibility if !! was used) */
    tb_textview_append(app, screen, line);
    tb_field_set(input, "");

    /* Store this command as the last command (only if not "!!" alone) */
    if (strcmp(expanded, "!!") != 0) {
        free(last_cmd);
        last_cmd = malloc(expanded_len + 1);
        if (last_cmd) strcpy(last_cmd, expanded);
    }

    if (to_sh < 0 && !start_shell(app)) {
        tb_textview_append(app, screen, "[cannot start /bin/sh]\n");
    } else if (!write_all(to_sh, line, expanded_len + 1)) {
        tb_textview_append(app, screen, "[write to shell failed]\n");
    }
    free(expanded);
    free(line);
}

/* ---- main --------------------------------------------------------------- */

/* Transcript above the prompt strip, prompt strip at the bottom. */
static void layout(tb_app *app) {
    int py = app->height - TT_PROMPT_H;
    tb_textview_resize(app, screen, 0, 0, app->width, py);
    prompt->base.y = py + (TT_PROMPT_H - (app->fascent + app->fdescent)) / 2;
    input->base.x = 18;
    input->base.y = py + 2;
    input->base.w = app->width - 24;
    input->base.h = TT_PROMPT_H - 4;
    tb_damage_all(app);
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

    screen = tb_textview_new(app, 0, 0, TT_W, TT_H - TT_PROMPT_H);
    tb_textview_set_wrap(app, screen, 1);   /* wrapped lines, no horizontal bar */

    /* prompt strip: "$" then the borderless input field */
    prompt = tb_label_new(app, 6, 0, TB_C_FG, "$");
    input = tb_field_new(app, 18, 0, TT_W - 24, 0);
    input->plain = 1;
    tb_field_on_accept(input, on_enter, NULL);
    app->root.focus = &input->base;

    layout(app);
    tb_on_resize(app, on_resize, NULL);

    start_shell(app);
    tb_textview_append(app, screen, "Teletype. Type a command and press Return.\n");

    tb_run(app);

    /* Quit: ask the shell to stop, then reap it. */
    if (sh_pid > 0) {
        kill(sh_pid, SIGTERM);
        waitpid(sh_pid, NULL, 0);
    }
    if (from_sh >= 0) close(from_sh);
    if (to_sh >= 0) close(to_sh);
    tb_app_free(app);
    return 0;
}
