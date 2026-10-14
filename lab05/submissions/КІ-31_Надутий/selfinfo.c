/* selfinfo — процес розповідає про себе (ЛР 5, частина 1).
 *
 *   selfinfo [-m] [-o ФАЙЛ] [-s СЕК] [-x КОД] [-k СИГНАЛ] [АРГ...]
 *
 *   -m         додати карту пам'яті: адреси змінних різних видів і регіон
 *              /proc/self/maps, у який кожна потрапляє
 *   -o ФАЙЛ    писати звіт у ФАЙЛ, а не в stdout (для демонів)
 *   -s СЕК     після звіту заснути на СЕК секунд
 *   -x КОД     завершитися з кодом КОД (за замовчуванням 0)
 *   -k СИГНАЛ  наприкінці надіслати собі сигнал СИГНАЛ (номер)
 *
 * Звіт — рядки «ключ значення», однаковий формат для людини й тесту.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

int initialized_global = 42;     /* .data */
int zero_global;                 /* .bss  */

#define MAXFD 64

struct fdinfo { int fd; char target[PATH_MAX]; };

/* Зібрати відкриті дескриптори процесу, крім службового дескриптора
 * самого каталогу /proc/self/fd. Повертає кількість або -1. */
static int collect_fds(struct fdinfo *out, int max)
{
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    int n = 0, own = dirfd(d);
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.') continue;
        int fd = atoi(e->d_name);
        if (fd == own) continue;
        char link[64];
        snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
        ssize_t len = readlink(link, out[n].target, sizeof out[n].target - 1);
        if (len < 0) len = 0;
        out[n].target[len] = '\0';
        out[n].fd = fd;
        n++;
    }
    closedir(d);
    /* readdir не гарантує порядку — сортуємо вставками */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && out[j - 1].fd > out[j].fd; j--) {
            struct fdinfo t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

/* Знайти в /proc/self/maps рядок, діапазон якого містить addr, і
 * скопіювати в name останнє поле (шлях, [heap], [stack]) або «anon». */
static int region_of(const void *addr, char *name, size_t size)
{
    /* TODO 1: відкрити /proc/self/maps і знайти рядок, діапазон якого
     * «початок-кінець» містить addr (початок включно, кінець — ні).
     * Записати в name права доступу і останнє поле рядка — шлях, [heap],
     * [stack] — або «anon», якщо поля немає: snprintf(name, size, "%s %s", ...).
     * Повернути 0, якщо знайдено, і -1 інакше. Формат рядка: man 5 proc,
     * розділ /proc/pid/maps. Підказка: sscanf з %lx і %n. */
    (void)addr;
    snprintf(name, size, "TODO");
    return -1;
}

static void print_region(FILE *out, const char *what, const void *addr)
{
    char name[PATH_MAX + 16];
    if (region_of(addr, name, sizeof name) != 0) strcpy(name, "?");
    fprintf(out, "addr %-8s %14p  %s\n", what, addr, name);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(int argc, char *argv[])
{
    int want_map = 0, code = 0, sig = 0;
    unsigned sleep_s = 0;
    const char *outpath = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "+mo:s:x:k:")) != -1) {
        switch (opt) {
        case 'm': want_map = 1; break;
        case 'o': outpath = optarg; break;
        case 's': sleep_s = (unsigned)atoi(optarg); break;
        case 'x': code = atoi(optarg); break;
        case 'k': sig = atoi(optarg); break;
        default:
            fprintf(stderr, "usage: selfinfo [-m] [-o FILE] [-s SEC] [-x CODE] [-k SIG] [ARG...]\n");
            return 2;
        }
    }

    /* Усе, що залежить від відкритих дескрипторів, збираємо ДО того,
     * як самі щось відкриємо (файл звіту, /proc/self/maps). */
    struct fdinfo fds[MAXFD];
    int nfd = collect_fds(fds, MAXFD);

    FILE *out = stdout;
    if (outpath) {
        out = fopen(outpath, "we");          /* "e" = O_CLOEXEC */
        if (!out) {
            fprintf(stderr, "selfinfo: %s: %s\n", outpath, strerror(errno));
            return 1;
        }
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
    mode_t um = umask(0); umask(um);

    /* Керуючий термінал: сьоме поле /proc/self/stat (tty_nr), 0 = немає */
    int tty_nr = -1;
    FILE *st = fopen("/proc/self/stat", "r");
    if (st) {
        char buf[1024];
        if (fgets(buf, sizeof buf, st)) {
            char *rp = strrchr(buf, ')');     /* ім'я команди може містити пробіли */
            if (rp) sscanf(rp + 2, "%*c %*d %*d %*d %d", &tty_nr);
        }
        fclose(st);
    }

    fprintf(out, "pid %d\nppid %d\npgid %d\nsid %d\n",
            (int)getpid(), (int)getppid(), (int)getpgrp(), (int)getsid(0));
    fprintf(out, "ctty %s\n", tty_nr == 0 ? "none" : "yes");
    fprintf(out, "cwd %s\numask %04o\n", cwd, (unsigned)um);
    fprintf(out, "argc %d\n", argc);
    for (int i = 0; i < argc; i++)
        fprintf(out, "argv[%d] %s\n", i, argv[i]);

    int nenv = 0;
    while (environ[nenv]) nenv++;
    char **env = malloc((size_t)nenv * sizeof *env);
    if (env) {
        memcpy(env, environ, (size_t)nenv * sizeof *env);
        qsort(env, (size_t)nenv, sizeof *env, cmp_str);
        for (int i = 0; i < nenv; i++) fprintf(out, "env %s\n", env[i]);
        free(env);
    }

    if (nfd < 0) fprintf(out, "fd ? %s\n", strerror(errno));
    for (int i = 0; i < nfd; i++)
        fprintf(out, "fd %d %s\n", fds[i].fd, fds[i].target);

    if (want_map) {
        int local = 0;
        void *small = malloc(64);
        void *big = malloc(1 << 20);          /* великий блок malloc бере через mmap */
        print_region(out, "main", (void *)main);
        print_region(out, "data", &initialized_global);
        print_region(out, "bss", &zero_global);
        print_region(out, "heap", small);
        print_region(out, "mmap", big);
        print_region(out, "libc", (void *)printf);
        print_region(out, "stack", &local);
        print_region(out, "argv", argv[0]);
        print_region(out, "environ", environ[0] ? environ[0] : (char *)environ);
        free(small);
        free(big);
    }

    if (fflush(out) == EOF || (out != stdout && fclose(out) == EOF)) {
        fprintf(stderr, "selfinfo: write: %s\n", strerror(errno));
        return 1;
    }
    if (sleep_s) sleep(sleep_s);
    if (sig) raise(sig);
    return code;
}
