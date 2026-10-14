/* forkcost — скільки коштує fork і що таке copy-on-write (ЛР 5, частина 4).
 *
 *   forkcost [-r ПОВТОРІВ] МіБ...
 *
 * Для кожного розміру: виділити й заповнити стільки МіБ пам'яті, потім
 *   fork_ms   — медіана часу виклику fork() у батька;
 *   flt_idle  — сторінкові збої дитини, яка одразу виходить;
 *   flt_write — збої дитини, яка записала по байту в кожну сторінку;
 *   spawn_ms  — медіана posix_spawn("/bin/true") для порівняння.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n)
{
    qsort(v, (size_t)n, sizeof *v, cmp_d);
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

/* Один fork. touch=1 — дитина пише в кожну сторінку буфера.
 * Повертає час fork() у батька, у *minflt — збої дитини (з wait4). */
static double one_fork(char *buf, size_t size, int touch, long *minflt)
{
    long page = sysconf(_SC_PAGESIZE);
    /* TODO 4: виміряти час самого виклику fork() у батька (now_ms до і після).
     * Дитина: якщо touch — змінити по одному байту в кожній сторінці buf
     * (крок page), потім одразу _exit(0) — чому не exit і не return?
     * Батько: дочекатися дитини через wait4 (не waitpid!) і записати в
     * *minflt поле ru_minflt — сторінкові збої саме цієї дитини.
     * Не забудьте EINTR. Повернути виміряний час у мілісекундах. */
    (void)buf; (void)size; (void)touch; (void)page;
    *minflt = 0;
    return 0;
}

static double one_spawn(void)
{
    char *argv[] = { "true", NULL };
    pid_t pid;
    double t0 = now_ms();
    int err = posix_spawn(&pid, "/bin/true", NULL, NULL, argv, environ);
    double t1 = now_ms();
    if (err) { fprintf(stderr, "forkcost: posix_spawn: %s\n", strerror(err)); exit(1); }
    int status;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) ;
    return t1 - t0;
}

int main(int argc, char *argv[])
{
    int reps = 5, opt;
    while ((opt = getopt(argc, argv, "r:")) != -1) {
        if (opt == 'r' && atoi(optarg) > 0) reps = atoi(optarg);
        else { fprintf(stderr, "usage: forkcost [-r REPEATS] MiB...\n"); return 2; }
    }
    if (optind >= argc) { fprintf(stderr, "usage: forkcost [-r REPEATS] MiB...\n"); return 2; }

    printf("%6s %10s %10s %10s %10s\n", "MiB", "fork_ms", "flt_idle", "flt_write", "spawn_ms");
    double *t = malloc((size_t)reps * sizeof *t);
    for (int a = optind; a < argc; a++) {
        size_t mib = strtoul(argv[a], NULL, 10);
        size_t size = mib << 20;
        char *buf = NULL;
        if (size) {
            buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (buf == MAP_FAILED) { perror("forkcost: mmap"); return 1; }
            memset(buf, 1, size);   /* без цього сторінок фізично ще немає */
        }
        long flt_idle = 0, flt_write = 0;
        one_fork(buf, size, 0, &flt_idle);        /* розігрів */
        for (int i = 0; i < reps; i++) t[i] = one_fork(buf, size, 0, &flt_idle);
        double fork_ms = median(t, reps);
        one_fork(buf, size, 1, &flt_write);
        for (int i = 0; i < reps; i++) t[i] = one_spawn();
        double spawn_ms = median(t, reps);
        printf("%6zu %10.3f %10ld %10ld %10.3f\n", mib, fork_ms, flt_idle, flt_write, spawn_ms);
        fflush(stdout);
        if (buf) munmap(buf, size);
    }
    free(t);
    return 0;
}
