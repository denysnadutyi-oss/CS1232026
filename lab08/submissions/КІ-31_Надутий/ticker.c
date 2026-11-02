/* ticker — періодична задача: дрейф і запізнення трьома способами чекати
 * (ЛР 8, частина 2).
 *
 *   ticker rel PERIOD_MS COUNT [WORK_MS]   після роботи спати PERIOD відносно (nanosleep)
 *   ticker abs PERIOD_MS COUNT [WORK_MS]   спати до абсолютного дедлайну (clock_nanosleep)
 *   ticker tfd PERIOD_MS COUNT [WORK_MS]   чекати на timerfd з інтервалом PERIOD
 *   ticker log PERIOD_MS                   нескінченний «пульс» для служби systemd (частина 5)
 *
 * На кожній ітерації програма WORK_MS мілісекунд «працює» (крутиться в циклі),
 * а потім чекає на наступний період. Вивід:
 *   mode M period_ms P count N work_ms W
 *   elapsed_ms E expected_ms X drift_ms D   E — від старту до останнього пробудження,
 *                                           X = (N + missed) · P, D = E − X
 *   late_us med A p99 B max C               на скільки пробудження пізніше за свою ціль
 *   missed K                                пропущені періоди (робота довша за період)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define NS_PER_MS 1000000LL
#define NS_PER_S  1000000000LL

static volatile sig_atomic_t stop_requested;

static void on_stop(int sig) { (void)sig; stop_requested = 1; }

static int64_t ts_ns(const struct timespec *t)
{
    return (int64_t)t->tv_sec * NS_PER_S + t->tv_nsec;
}

static struct timespec ns_ts(int64_t ns)
{
    struct timespec t = { .tv_sec = ns / NS_PER_S, .tv_nsec = ns % NS_PER_S };
    return t;
}

static int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return ts_ns(&t);
}

/* «Робота»: крутитися, доки не мине ns наносекунд. */
static void spin(int64_t ns)
{
    int64_t end = now_ns() + ns;
    while (now_ns() < end)
        ;
}

/* Чекати на наступний дедлайн періодичної задачі.
 * *next — дедлайн попередньої ітерації (час CLOCK_MONOTONIC); функція має
 * зсунути його на period_ns і заснути до нього. Якщо новий дедлайн уже
 * минув (робота тривала довше за період), пропустити всі минулі дедлайни,
 * додати їх кількість до *missed і чекати першого майбутнього. */
static void wait_abs(struct timespec *next, int64_t period_ns, long *missed)
{
    /* TODO 2. Зараз функція просто спить один період — так само, як режим rel,
     * і тому так само накопичує дрейф. Перепишіть її за описом вище:
     * дедлайн = попередній дедлайн + period_ns (а не «зараз» + period_ns);
     * якщо дедлайн <= now_ns(), пропустіть стільки цілих періодів, скільки
     * треба, щоб він став майбутнім, і порахуйте їх у *missed;
     * потім clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, …).
     * Увага: clock_nanosleep повертає номер помилки, а не −1 з errno.
     * Перерваний сигналом сон (EINTR) повторіть, якщо не встановлено
     * stop_requested. */
    (void)missed;
    struct timespec p = ns_ts(period_ns);
    nanosleep(&p, NULL);
    *next = ns_ts(now_ns());
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static int run(const char *mode, int64_t period, long count, int64_t work)
{
    int64_t *late = calloc((size_t)count, sizeof *late);
    if (!late) { perror("calloc"); return 1; }
    long missed = 0;
    int64_t start = now_ns(), wake = start;

    if (strcmp(mode, "rel") == 0) {
        for (long i = 0; i < count; i++) {
            spin(work);
            int64_t asked = now_ns();
            struct timespec p = ns_ts(period);
            nanosleep(&p, NULL);
            wake = now_ns();
            late[i] = wake - (asked + period);
        }
    } else if (strcmp(mode, "abs") == 0) {
        struct timespec next = ns_ts(start);
        for (long i = 0; i < count; i++) {
            spin(work);
            wait_abs(&next, period, &missed);
            wake = now_ns();
            late[i] = wake - ts_ns(&next);
        }
    } else if (strcmp(mode, "tfd") == 0) {
        int fd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (fd < 0) { perror("timerfd_create"); return 1; }
        struct itimerspec its = { .it_value = ns_ts(start + period), .it_interval = ns_ts(period) };
        if (timerfd_settime(fd, TFD_TIMER_ABSTIME, &its, NULL) < 0) { perror("timerfd_settime"); return 1; }
        int64_t fired = 0;
        for (long i = 0; i < count; i++) {
            spin(work);
            uint64_t exp;
            if (read(fd, &exp, sizeof exp) != (ssize_t)sizeof exp) { perror("read timerfd"); return 1; }
            wake = now_ns();
            fired += (int64_t)exp;
            missed += (long)exp - 1;
            late[i] = wake - (start + fired * period);
        }
        close(fd);
    } else {
        fprintf(stderr, "ticker: невідомий режим %s\n", mode);
        return 2;
    }

    int64_t elapsed = wake - start, expected = (count + missed) * period;
    qsort(late, (size_t)count, sizeof *late, cmp_i64);
    printf("mode %s period_ms %.3f count %ld work_ms %.3f\n", mode,
           (double)period / NS_PER_MS, count, (double)work / NS_PER_MS);
    printf("elapsed_ms %.3f expected_ms %.3f drift_ms %.3f\n", (double)elapsed / NS_PER_MS,
           (double)expected / NS_PER_MS, (double)(elapsed - expected) / NS_PER_MS);
    printf("late_us med %.1f p99 %.1f max %.1f\n", late[count / 2] / 1e3,
           late[(count * 99) / 100] / 1e3, late[count - 1] / 1e3);
    printf("missed %ld\n", missed);
    free(late);
    return 0;
}

/* Нескінченний пульс: рядок на кожен період, доки не прийде SIGTERM або SIGINT. */
static int run_log(int64_t period)
{
    /* stdout служби — не термінал, а канал до journald. Без цього рядка
     * printf накопичував би вивід у буфері на 4 КіБ, і журнал мовчав би. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction sa = { .sa_handler = on_stop };
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    long n = 0, missed = 0;
    int64_t start = now_ns();
    struct timespec next = ns_ts(start);
    printf("ticker started period_ms %lld pid %d\n", (long long)(period / NS_PER_MS), (int)getpid());
    while (!stop_requested) {
        wait_abs(&next, period, &missed);
        if (stop_requested) break;
        int64_t late = now_ns() - ts_ns(&next);
        struct timespec rt;
        clock_gettime(CLOCK_REALTIME, &rt);
        struct tm tm;
        gmtime_r(&rt.tv_sec, &tm);
        char buf[32];
        strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
        printf("tick %ld mono_s %.3f real %s.%03ldZ late_us %.1f missed %ld\n", ++n,
               (double)(ts_ns(&next) - start) / NS_PER_S, buf, rt.tv_nsec / 1000000,
               late / 1e3, missed);
    }
    printf("stopped after %ld ticks\n", n);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "log") == 0) {
        double p = atof(argv[2]);
        if (p <= 0) { fprintf(stderr, "ticker: PERIOD_MS має бути додатним\n"); return 2; }
        return run_log((int64_t)(p * NS_PER_MS));
    }
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "usage: ticker rel|abs|tfd PERIOD_MS COUNT [WORK_MS] | ticker log PERIOD_MS\n");
        return 2;
    }
    double p = atof(argv[2]), w = argc == 5 ? atof(argv[4]) : 0;
    long count = atol(argv[3]);
    if (p <= 0 || count <= 0 || w < 0) {
        fprintf(stderr, "ticker: PERIOD_MS і COUNT мають бути додатними, WORK_MS — невід'ємним\n");
        return 2;
    }
    return run(argv[1], (int64_t)(p * NS_PER_MS), count, (int64_t)(w * NS_PER_MS));
}
