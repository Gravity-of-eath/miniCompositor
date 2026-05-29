/*
 * mc-launcher — minimal supervisor for the mc framework.
 *
 * Responsibilities:
 *   - Read launcher.conf (simple ini)
 *   - Spawn mc-compositor + configured clients
 *   - Reap exited children via signalfd(SIGCHLD)
 *   - Restart with exponential backoff (caps at 5 fast crashes / 30s gap)
 *   - On SIGINT/SIGTERM: SIGTERM all children, give them 2s, then SIGKILL
 *
 * Out of scope (intentional):
 *   - Dependency graph (we just start compositor first, then clients with
 *     a delay each — good enough since clients fail fast and get restarted
 *     when the compositor is up)
 *   - Health checks beyond exit status
 *   - File log rotation; we just inherit stdout/stderr
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_SERVICES 16
#define MAX_ARGS     16

static int g_log_level = 2;
#define LOG_AT(lvl, tag, ...) do { \
    if (g_log_level >= (lvl)) { \
        struct timespec _ts; clock_gettime(CLOCK_MONOTONIC, &_ts); \
        fprintf(stderr, "[%ld.%03ld][" tag "][launcher] ", \
            (long)_ts.tv_sec, (long)(_ts.tv_nsec / 1000000)); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
    } \
} while (0)
#define LOG_E(...) LOG_AT(0, "E", __VA_ARGS__)
#define LOG_W(...) LOG_AT(1, "W", __VA_ARGS__)
#define LOG_I(...) LOG_AT(2, "I", __VA_ARGS__)
#define LOG_D(...) LOG_AT(3, "D", __VA_ARGS__)

struct service {
    char  name[32];
    char  binary[256];
    char *args_storage;             /* mutable copy of args string */
    char *argv[MAX_ARGS + 2];       /* argv[0] = binary; trailing NULL */
    int   respawn;
    int   autostart;
    int   delay_ms;
    int   is_compositor;

    pid_t pid;                      /* 0 = not running */
    int   crash_streak;             /* consecutive fast crashes */
    struct timespec last_start;

    int   pending_restart;
    struct timespec restart_at;
};

static struct service g_svc[MAX_SERVICES];
static int g_n_svc = 0;
static int g_running = 1;

/* ---- time helpers ---- */
static void ts_add_ms(struct timespec *ts, int ms)
{
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static long ts_diff_ms(const struct timespec *a, const struct timespec *b)
{
    /* a - b in ms */
    return (long)(a->tv_sec - b->tv_sec) * 1000
         + (long)(a->tv_nsec - b->tv_nsec) / 1000000L;
}

/* ---- service spawn/reap ---- */

static int spawn_one(struct service *s)
{
    pid_t pid = fork();
    if (pid < 0) {
        LOG_E("fork %s: %s", s->name, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        /* child: ask kernel to send us SIGTERM if launcher dies */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        /* restore default signal handling for SIGINT/SIGTERM/SIGCHLD */
        sigset_t empty; sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, NULL);
        execv(s->binary, s->argv);
        fprintf(stderr, "[launcher] execv %s failed: %s\n",
                s->binary, strerror(errno));
        _exit(127);
    }
    s->pid = pid;
    clock_gettime(CLOCK_MONOTONIC, &s->last_start);
    LOG_I("started %s (pid=%d binary=%s)", s->name, pid, s->binary);
    return 0;
}

static void schedule_restart(struct service *s)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long uptime_ms = ts_diff_ms(&now, &s->last_start);

    /* "Fast crash" = lived under 5 seconds. Reset the streak otherwise. */
    if (uptime_ms < 5000) s->crash_streak++;
    else                  s->crash_streak = 0;

    if (s->crash_streak > 5) {
        LOG_E("%s crashed %d times rapidly — giving up. Restart launcher to retry.",
              s->name, s->crash_streak);
        return;
    }

    /* 500ms × 2^streak, capped at 30s */
    int delay = 500;
    for (int i = 0; i < s->crash_streak; i++) delay *= 2;
    if (delay > 30000) delay = 30000;

    s->pending_restart = 1;
    s->restart_at = now;
    ts_add_ms(&s->restart_at, delay);
    LOG_W("will restart %s in %d ms (fast-crash#%d)",
          s->name, delay, s->crash_streak);
}

static void reap_children(void)
{
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            if (pid < 0 && errno != ECHILD)
                LOG_W("waitpid: %s", strerror(errno));
            return;
        }
        int ec = -1, sig = 0;
        if (WIFEXITED(status))   ec  = WEXITSTATUS(status);
        if (WIFSIGNALED(status)) sig = WTERMSIG(status);

        struct service *s = NULL;
        for (int i = 0; i < g_n_svc; i++) {
            if (g_svc[i].pid == pid) { s = &g_svc[i]; break; }
        }
        if (!s) {
            LOG_W("orphan child pid=%d exited", pid);
            continue;
        }
        s->pid = 0;
        const char *reason = sig ? "killed" : (ec == 0 ? "ok" : "fail");
        LOG_I("%s exited (pid=%d ec=%d sig=%d) %s",
              s->name, pid, ec, sig, reason);

        if (!g_running) continue;              /* shutting down */
        if (!s->respawn) continue;             /* one-shot service */
        if (sig == 0 && ec == 0) {
            /* Clean exit (e.g. popup OK was clicked). Don't auto-restart;
             * launcher treats this as "user said done". */
            LOG_I("%s exited cleanly, NOT restarting", s->name);
            continue;
        }
        schedule_restart(s);
    }
}

/* ---- shutdown ---- */

static void shutdown_all(int sfd)
{
    LOG_I("shutdown: sending SIGTERM to all children");
    g_running = 0;
    for (int i = 0; i < g_n_svc; i++) {
        if (g_svc[i].pid > 0) kill(g_svc[i].pid, SIGTERM);
    }
    /* Drain signalfd while waiting (need to read it or it stays "ready") */
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    ts_add_ms(&deadline, 2000);
    while (1) {
        int any_alive = 0;
        for (int i = 0; i < g_n_svc; i++)
            if (g_svc[i].pid > 0) { any_alive = 1; break; }
        if (!any_alive) break;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long left = ts_diff_ms(&deadline, &now);
        if (left <= 0) break;

        struct pollfd p = { .fd = sfd, .events = POLLIN };
        int r = poll(&p, 1, (int)left);
        if (r < 0 && errno == EINTR) continue;
        if (r > 0 && (p.revents & POLLIN)) {
            struct signalfd_siginfo si;
            while (read(sfd, &si, sizeof(si)) == sizeof(si)) {
                if (si.ssi_signo == SIGCHLD) reap_children();
            }
        }
    }
    /* Anyone still alive gets SIGKILL */
    for (int i = 0; i < g_n_svc; i++) {
        if (g_svc[i].pid > 0) {
            LOG_W("force-killing %s (pid=%d)", g_svc[i].name, g_svc[i].pid);
            kill(g_svc[i].pid, SIGKILL);
            waitpid(g_svc[i].pid, NULL, 0);
            g_svc[i].pid = 0;
        }
    }
}

/* ---- config parsing (minimal ini) ---- */

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int parse_bool(const char *v)
{
    return strcmp(v, "yes") == 0 || strcmp(v, "1") == 0 ||
           strcmp(v, "true") == 0 || strcmp(v, "on") == 0;
}

/* Split `s` in-place by ASCII whitespace, append pointers to argv[]
 * starting at *idx. Updates *idx. */
static void split_argv(char *s, char **argv, int *idx, int cap)
{
    char *p = s;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) *p++ = 0;
        if (!*p) break;
        if (*idx >= cap - 1) break;
        argv[(*idx)++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
    }
    argv[*idx] = NULL;
}

static int parse_conf(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_E("open config %s: %s", path, strerror(errno));
        return -1;
    }

    char line[512];
    struct service *cur = NULL;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == 0 || *s == '#' || *s == ';') continue;

        /* section header [name] */
        size_t l = strlen(s);
        if (s[0] == '[' && s[l-1] == ']') {
            s[l-1] = 0;
            char *section = s + 1;
            if (g_n_svc >= MAX_SERVICES) {
                LOG_W("too many services, dropping [%s]", section);
                cur = NULL; continue;
            }
            cur = &g_svc[g_n_svc++];
            memset(cur, 0, sizeof(*cur));
            cur->respawn   = 1;
            cur->autostart = 1;
            cur->delay_ms  = 0;
            if (strcmp(section, "compositor") == 0) {
                snprintf(cur->name, sizeof(cur->name), "compositor");
                cur->is_compositor = 1;
            } else if (strncmp(section, "client:", 7) == 0) {
                snprintf(cur->name, sizeof(cur->name), "%s", section + 7);
                if (cur->delay_ms == 0) cur->delay_ms = 500;
            } else {
                LOG_W("unknown section [%s] -- ignored", section);
                g_n_svc--;
                cur = NULL;
            }
            continue;
        }

        if (!cur) continue;

        /* key = value */
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s);
        char *v = trim(eq + 1);

        if (strcmp(k, "binary") == 0) {
            snprintf(cur->binary, sizeof(cur->binary), "%s", v);
        } else if (strcmp(k, "args") == 0) {
            free(cur->args_storage);
            cur->args_storage = strdup(v);
        } else if (strcmp(k, "respawn") == 0) {
            cur->respawn = parse_bool(v);
        } else if (strcmp(k, "autostart") == 0) {
            cur->autostart = parse_bool(v);
        } else if (strcmp(k, "delay_ms") == 0) {
            cur->delay_ms = atoi(v);
        } else {
            LOG_W("unknown key '%s' in [%s]", k, cur->name);
        }
    }
    fclose(f);

    /* Finalize argv arrays after the whole file is parsed (so binary= and
     * args= can appear in any order). */
    int found_comp = 0;
    for (int i = 0; i < g_n_svc; i++) {
        struct service *s = &g_svc[i];
        if (s->is_compositor) found_comp = 1;
        if (s->binary[0] == 0) {
            LOG_E("service %s missing 'binary='", s->name);
            return -1;
        }
        s->argv[0] = s->binary;
        int idx = 1;
        if (s->args_storage) split_argv(s->args_storage, s->argv, &idx, MAX_ARGS);
        s->argv[idx] = NULL;
        LOG_I("svc[%d] %s binary=%s autostart=%d respawn=%d delay=%dms",
              i, s->name, s->binary, s->autostart, s->respawn, s->delay_ms);
    }
    if (!found_comp) {
        LOG_W("no [compositor] section in config — clients will fail to connect");
    }
    return 0;
}

/* ---- main loop ---- */

int main(int argc, char **argv)
{
    const char *cfg = "/etc/mc/launcher.conf";

    static const struct option opts[] = {
        {"config",  required_argument, 0, 'c'},
        {"verbose", no_argument,       0, 'v'},
        {"quiet",   no_argument,       0, 'q'},
        {"help",    no_argument,       0,  0 },
        {0,0,0,0}
    };
    int c, idx;
    while ((c = getopt_long(argc, argv, "c:vq", opts, &idx)) != -1) {
        switch (c) {
        case 'c': cfg = optarg; break;
        case 'v': g_log_level++; break;
        case 'q': g_log_level--; break;
        case 0:
        default:
            fprintf(stderr,
                "Usage: %s [-c PATH] [-v] [-q]\n"
                "  -c PATH   config file (default /etc/mc/launcher.conf)\n",
                argv[0]);
            return c == 0 ? 0 : 1;
        }
    }

    if (parse_conf(cfg) < 0) return 1;
    if (g_n_svc == 0) {
        LOG_E("no services defined");
        return 1;
    }

    /* Block the signals we want signalfd to deliver. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    /* SFD_NONBLOCK is critical: we drain in a while-read loop, and without
     * it the second read() would block waiting for the next signal -- the
     * very thing we just finished draining. */
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0) {
        LOG_E("signalfd: %s", strerror(errno));
        return 1;
    }
    /* Avoid getting EPIPE killed when a child socket closes during writes. */
    signal(SIGPIPE, SIG_IGN);

    /* Start compositor immediately; queue clients with their delay. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    for (int i = 0; i < g_n_svc; i++) {
        if (!g_svc[i].autostart) continue;
        if (g_svc[i].is_compositor) {
            spawn_one(&g_svc[i]);
        } else {
            g_svc[i].pending_restart = 1;
            g_svc[i].restart_at = now;
            ts_add_ms(&g_svc[i].restart_at, g_svc[i].delay_ms);
        }
    }

    while (g_running) {
        /* Compute next due restart so we don't oversleep. */
        int timeout_ms = -1;
        clock_gettime(CLOCK_MONOTONIC, &now);
        for (int i = 0; i < g_n_svc; i++) {
            if (!g_svc[i].pending_restart || g_svc[i].pid != 0) continue;
            long left = ts_diff_ms(&g_svc[i].restart_at, &now);
            if (left < 0) left = 0;
            if (timeout_ms < 0 || (int)left < timeout_ms) timeout_ms = (int)left;
        }

        struct pollfd p = { .fd = sfd, .events = POLLIN };
        int r = poll(&p, 1, timeout_ms);
        if (r < 0) {
            if (errno == EINTR) continue;
            LOG_E("poll: %s", strerror(errno));
            break;
        }
        if (r > 0 && (p.revents & POLLIN)) {
            struct signalfd_siginfo si;
            while (read(sfd, &si, sizeof(si)) == sizeof(si)) {
                if (si.ssi_signo == SIGCHLD) {
                    reap_children();
                } else if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM) {
                    LOG_I("got signal %u, shutting down", si.ssi_signo);
                    shutdown_all(sfd);
                    goto done;
                }
            }
        }

        /* Start any scheduled services whose time has come. */
        clock_gettime(CLOCK_MONOTONIC, &now);
        for (int i = 0; i < g_n_svc; i++) {
            if (!g_svc[i].pending_restart || g_svc[i].pid != 0) continue;
            if (ts_diff_ms(&g_svc[i].restart_at, &now) <= 0) {
                g_svc[i].pending_restart = 0;
                spawn_one(&g_svc[i]);
            }
        }
    }

done:
    close(sfd);
    LOG_I("bye");
    return 0;
}
