/* launch — запуск програми в контрольованому оточенні (ЛР 5, частина 2–3).
 *
 *   launch [-e ІМ'Я=ЗНАЧЕННЯ]... [-u ІМ'Я]... [-C КАТАЛОГ] [-o ФАЙЛ]
 *          [-l ЖУРНАЛ] [-d [-p PIDФАЙЛ]] [--] ПРОГРАМА [АРГ...]
 *
 * Специфікація — у lab05/README.md. Завдання позначено TODO 2 і TODO 3.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXENV 64

/* Що дитина повідомляє батькові через канал звіту */
enum stage { ST_PID = 1, ST_CHDIR, ST_OPEN, ST_EXEC };
struct report { int stage; int value; };   /* value: PID або errno */

struct opts {
    char *set[MAXENV]; int nset;
    char *unset[MAXENV]; int nunset;
    const char *dir, *out, *log, *pidfile;
    int daemon;
    char **argv;
};

static int logfd = -1;

static void logmsg(const char *fmt, ...)
{
    if (logfd < 0) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = sizeof buf - 1;
    if (write(logfd, buf, (size_t)n) < 0) { /* журнал — не критично */ }
}

static void usage(void)
{
    fprintf(stderr, "usage: launch [-e NAME=VALUE]... [-u NAME]... [-C DIR] [-o FILE]\n"
                    "              [-l LOG] [-d [-p PIDFILE]] [--] PROGRAM [ARG...]\n");
    exit(2);
}

/* Записати повідомлення в канал і завершити дитину. Лише async-signal-safe
 * виклики: після fork у дитині не можна покладатися на stdio чи malloc. */
static void child_fail(int wfd, int stage, int err)
{
    struct report r = { stage, err };
    ssize_t n = write(wfd, &r, sizeof r);
    (void)n;
    _exit(stage == ST_EXEC ? (err == ENOENT ? 127 : 126) : 1);
}

/* Усе, що дитина робить між fork і exec. Не повертається.
 * wfd — кінець каналу звіту для запису (з O_CLOEXEC). */
static void child_setup_and_exec(const struct opts *o, int wfd)
{
    /* TODO 2a, у такому порядку:
     *   1) -C: chdir; помилка → child_fail(wfd, ST_CHDIR, errno);
     *   2) -u: unsetenv для кожного імені, потім -e: putenv для кожної пари;
     *   3) -o: open(ФАЙЛ, O_WRONLY | O_CREAT | O_TRUNC | ..., 0644) і dup2 на 1;
     *      у режимі -d ще й stdin ← /dev/null, stdout і stderr ← ФАЙЛ або
     *      /dev/null; будь-яка помилка → child_fail(wfd, ST_OPEN, errno);
     *   4) execvp; якщо повернувся → child_fail(wfd, ST_EXEC, errno).
     * Після fork у дитині — лише async-signal-safe виклики (man 7 signal-safety):
     * жодного printf, жодного exit. Чому? */
    (void)o;
    child_fail(wfd, ST_EXEC, ENOSYS);
}

static const char *stage_name(const struct opts *o, int stage)
{
    switch (stage) {
    case ST_CHDIR: return o->dir;
    case ST_OPEN:  return o->out ? o->out : "/dev/null";
    default:       return o->argv[0];
    }
}

/* Прочитати звіт дитини. 0 — канал закрився без помилки (exec вдався);
 * 1 — у *r помилка; -1 — збій самого read. */
static int read_report(int rfd, struct report *r)
{
    for (;;) {
        ssize_t n = read(rfd, r, sizeof *r);
        if (n == (ssize_t)sizeof *r) return 1;
        if (n == 0) return 0;
        if (n == -1 && errno == EINTR) continue;
        return -1;
    }
}

static pid_t wait_child(pid_t pid, int *status)
{
    pid_t w;
    while ((w = waitpid(pid, status, 0)) == -1 && errno == EINTR)
        ;
    return w;
}

static int run_foreground(const struct opts *o)
{
    /* TODO 2b:
     *   1) канал звіту: pipe2(..., O_CLOEXEC) — чому саме з цим прапорцем?
     *   2) fflush(NULL), fork; у дитині закрити кінець для читання і викликати
     *      child_setup_and_exec;
     *   3) у батька закрити кінець для запису, logmsg("start pid=%d cmd=%s\n", ...),
     *      read_report: 1 → дитина повідомила про помилку, 0 → exec вдався;
     *   4) wait_child; розібрати статус макросами WIFEXITED / WEXITSTATUS /
     *      WIFSIGNALED / WTERMSIG і повернути код за специфікацією;
     *      у журнал — "exit pid=%d code=%d\n" або "signal pid=%d sig=%d\n". */
    (void)o;
    fprintf(stderr, "launch: not implemented\n");
    return 1;
}

static int run_daemon(const struct opts *o)
{
    /* TODO 3: подвійний fork, setsid, umask(022), chdir("/") (якщо немає -C),
     * PID онука — першим повідомленням ST_PID у канал звіту, потім
     * child_setup_and_exec. Батько: дочекатися проміжного процесу, прочитати
     * PID демона, потім дочекатися EOF або помилки; у разі успіху — PID-файл
     * і "daemon pid=%d cmd=%s\n" у журнал. Покроково — у частині 3 роботи. */
    (void)o;
    fprintf(stderr, "launch: -d not implemented\n");
    return 1;
}

int main(int argc, char *argv[])
{
    struct opts o = {0};
    int opt;
    while ((opt = getopt(argc, argv, "+e:u:C:o:l:dp:")) != -1) {
        switch (opt) {
        case 'e':
            if (!strchr(optarg, '=') || optarg[0] == '=' || o.nset == MAXENV) usage();
            o.set[o.nset++] = optarg;
            break;
        case 'u':
            if (!*optarg || strchr(optarg, '=') || o.nunset == MAXENV) usage();
            o.unset[o.nunset++] = optarg;
            break;
        case 'C': o.dir = optarg; break;
        case 'o': o.out = optarg; break;
        case 'l': o.log = optarg; break;
        case 'd': o.daemon = 1; break;
        case 'p': o.pidfile = optarg; break;
        default: usage();
        }
    }
    if (optind >= argc || (o.pidfile && !o.daemon)) usage();
    o.argv = argv + optind;

    if (o.log) {
        logfd = open(o.log, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logfd == -1) {
            fprintf(stderr, "launch: %s: %s\n", o.log, strerror(errno));
            return 1;
        }
    }
    int rc = o.daemon ? run_daemon(&o) : run_foreground(&o);
    if (logfd != -1) close(logfd);
    return rc;
}
