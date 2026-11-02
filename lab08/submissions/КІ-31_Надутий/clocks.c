/* clocks — годинники ядра: роздільна здатність, крок, ціна читання і сон
 * (ЛР 8, частина 1).
 *
 *   clocks info          для кожного годинника: clock_getres і поточне значення
 *   clocks cost [N]      ціна одного читання: через vDSO і через системний виклик
 *   clocks step          найменший ненульовий крок між двома читаннями підряд
 *   clocks sleep [SLACK] скільки насправді триває сон 1 мкс … 10 мс;
 *                        SLACK — timer slack потоку в нс (prctl), за замовчуванням не змінюється
 *
 * Вивід:
 *   clock NAME RES_NS SEC.NSEC
 *   cost NAME VDSO_NS SYSCALL_NS       медіана 11 серій по N читань
 *   step NAME NS
 *   sleep REQ_US MEDIAN_US OVER_US     медіана 50 снів і перевищення над запитаним
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_TAI
#define CLOCK_TAI 11
#endif

struct clk { clockid_t id; const char *name; };

static const struct clk CLOCKS[] = {
    { CLOCK_REALTIME,           "REALTIME" },
    { CLOCK_REALTIME_COARSE,    "REALTIME_COARSE" },
    { CLOCK_MONOTONIC,          "MONOTONIC" },
    { CLOCK_MONOTONIC_COARSE,   "MONOTONIC_COARSE" },
    { CLOCK_MONOTONIC_RAW,      "MONOTONIC_RAW" },
    { CLOCK_BOOTTIME,           "BOOTTIME" },
    { CLOCK_TAI,                "TAI" },
    { CLOCK_PROCESS_CPUTIME_ID, "PROCESS_CPUTIME" },
    { CLOCK_THREAD_CPUTIME_ID,  "THREAD_CPUTIME" },
};
#define NCLOCKS (sizeof CLOCKS / sizeof CLOCKS[0])

static int64_t ts_ns(const struct timespec *t)
{
    return (int64_t)t->tv_sec * 1000000000 + t->tv_nsec;
}

/* Монотонний час у наносекундах — ним міряємо все інше. */
static int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return ts_ns(&t);
}

/* Прочитати годинник: raw = 0 — звичайний clock_gettime з glibc (vDSO, якщо
 * ядро його підтримує для цього годинника); raw = 1 — завжди системний виклик. */
static int read_clock(clockid_t id, int raw, struct timespec *t)
{
    if (raw)
        return (int)syscall(SYS_clock_gettime, id, t);
    return clock_gettime(id, t);
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

#define SERIES 11

/* Ціна одного читання годинника id у наносекундах.
 * Повертає медіану SERIES серій, у кожній — n читань підряд. */
static double cost_ns(clockid_t id, int raw, long n)
{
    /* TODO 1. Виміряйте, скільки коштує одне читання годинника id.
     * Одне читання триває десятки наносекунд — менше, ніж похибка одного
     * вимірювання, тому міряйте серію з n читань (read_clock) годинником
     * MONOTONIC (now_ns) і діліть на n. Повторіть SERIES разів і поверніть
     * медіану (qsort + cmp_double). Перед вимірюванням зробіть кілька
     * тисяч читань «на розігрів». */
    (void)id; (void)raw; (void)n; (void)read_clock;
    return 0.0;
}

static void cmd_info(void)
{
    for (size_t i = 0; i < NCLOCKS; i++) {
        struct timespec r, t;
        if (clock_getres(CLOCKS[i].id, &r) != 0 || clock_gettime(CLOCKS[i].id, &t) != 0) {
            printf("clock %s unsupported\n", CLOCKS[i].name);
            continue;
        }
        printf("clock %s %lld %lld.%09ld\n", CLOCKS[i].name, (long long)ts_ns(&r),
               (long long)t.tv_sec, t.tv_nsec);
    }
}

static void cmd_cost(long n)
{
    for (size_t i = 0; i < NCLOCKS; i++)
        printf("cost %s %.1f %.1f\n", CLOCKS[i].name,
               cost_ns(CLOCKS[i].id, 0, n), cost_ns(CLOCKS[i].id, 1, n));
}

/* Найменша ненульова різниця між двома читаннями підряд: так видно,
 * з яким кроком годинник насправді змінюється. */
static void cmd_step(void)
{
    for (size_t i = 0; i < NCLOCKS; i++) {
        struct timespec a, b;
        int64_t best = INT64_MAX;
        int64_t deadline = now_ns() + 200000000;      /* не довше 0,2 с на годинник */
        for (long k = 0; k < 200000 && now_ns() < deadline; k++) {
            clock_gettime(CLOCKS[i].id, &a);
            do clock_gettime(CLOCKS[i].id, &b); while (ts_ns(&b) == ts_ns(&a) && now_ns() < deadline);
            int64_t d = ts_ns(&b) - ts_ns(&a);
            if (d > 0 && d < best) best = d;
        }
        printf("step %s %lld\n", CLOCKS[i].name, best == INT64_MAX ? -1LL : (long long)best);
    }
}

static void cmd_sleep(long slack)
{
    if (slack > 0 && prctl(PR_SET_TIMERSLACK, slack, 0, 0, 0) != 0)
        perror("prctl PR_SET_TIMERSLACK");
    printf("slack %d\n", prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0));
    static const long req_us[] = { 1, 10, 100, 1000, 10000 };
    for (size_t i = 0; i < sizeof req_us / sizeof req_us[0]; i++) {
        double got[50];
        struct timespec r = { .tv_sec = 0, .tv_nsec = req_us[i] * 1000 };
        for (int k = 0; k < 50; k++) {
            int64_t t0 = now_ns();
            nanosleep(&r, NULL);
            got[k] = (double)(now_ns() - t0) / 1000.0;
        }
        qsort(got, 50, sizeof got[0], cmp_double);
        printf("sleep %ld %.1f %.1f\n", req_us[i], got[25], got[25] - (double)req_us[i]);
    }
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "info") == 0) { cmd_info(); return 0; }
    if (argc >= 2 && strcmp(argv[1], "cost") == 0) {
        long n = argc > 2 ? atol(argv[2]) : 100000;
        if (n <= 0) { fprintf(stderr, "clocks: N має бути додатним\n"); return 2; }
        cmd_cost(n);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "step") == 0) { cmd_step(); return 0; }
    if (argc >= 2 && strcmp(argv[1], "sleep") == 0) {
        cmd_sleep(argc > 2 ? atol(argv[2]) : 0);
        return 0;
    }
    fprintf(stderr, "usage: clocks info | cost [N] | step | sleep [SLACK_NS]\n");
    return 2;
}
