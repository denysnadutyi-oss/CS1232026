/* whenis — місцевий час, UTC і переходи на літній час (ЛР 8, частина 3).
 *
 *   whenis now ЗОНА…                       поточний час у кожній зоні
 *   whenis conv 'РРРР-ММ-ДД ГГ:ХХ' ЗОНА [ІНША…]
 *                                          місцевий час у ЗОНІ → UTC і інші зони
 *   whenis daily ГГ:ХХ ЗОНА РРРР-ММ-ДД ДНІВ
 *                                          щоденна подія о ГГ:ХХ за часом ЗОНИ:
 *                                          коли це за UTC і скільки годин між подіями
 * ЗОНА — назва з бази tz (Europe/Kyiv, America/New_York, UTC).
 *
 * Вивід conv:
 *   wall 2026-10-25 03:30 Europe/Kyiv
 *   status ok|gap|ambiguous N            N — кількість моментів з таким місцевим часом
 *   utc 2026-10-25T00:30:00Z EEST +0300  по рядку на кожен момент
 *   in America/New_York 2026-10-24 20:30 EDT -0400
 * Вивід daily:
 *   day 2026-10-25 utc 2026-10-25T07:00:00Z hours_since_prev 25.0
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Зробити ЗОНУ поточною для localtime_r і mktime цього процесу.
 * Невідому назву glibc мовчки трактує як UTC, тому перевіряємо файл зони. */
static int use_zone(const char *zone)
{
    if (strcmp(zone, "UTC") != 0) {
        char path[512];
        snprintf(path, sizeof path, "/usr/share/zoneinfo/%s", zone);
        FILE *f = fopen(path, "r");
        if (!f) { fprintf(stderr, "whenis: невідома зона %s\n", zone); return -1; }
        fclose(f);
    }
    if (setenv("TZ", zone, 1) != 0) { perror("setenv"); return -1; }
    tzset();
    return 0;
}

static int parse_wall(const char *s, struct tm *tm)
{
    memset(tm, 0, sizeof *tm);
    const char *end = strptime(s, "%Y-%m-%d %H:%M", tm);
    if (!end || *end) { fprintf(stderr, "whenis: очікувалося 'РРРР-ММ-ДД ГГ:ХХ', а не '%s'\n", s); return -1; }
    return 0;
}

static void print_utc(time_t t)
{
    struct tm u, l;
    char ub[32], lb[32];
    gmtime_r(&t, &u);
    localtime_r(&t, &l);
    strftime(ub, sizeof ub, "%Y-%m-%dT%H:%M:%SZ", &u);
    strftime(lb, sizeof lb, "%Z %z", &l);
    printf("utc %s %s\n", ub, lb);
}

/* Усі моменти часу, коли в ЗОНІ на годиннику був місцевий час wall
 * (поля tm_year … tm_min; tm_isdst не задано).
 * Записує їх у out за зростанням і повертає кількість: 1 — звичайний час,
 * 0 — такого часу не було (годинник перескочив уперед),
 * 2 — він був двічі (годинник повернули назад). */
static int wall_to_utc(const char *zone, const struct tm *wall, time_t out[2])
{
    if (use_zone(zone) != 0) return -1;
    /* TODO 3. Зараз функція просто довіряє mktime: для неіснуючого часу
     * вона поверне «виправлений» момент, а з двох можливих — лише один.
     * Перепишіть за алгоритмом:
     *   для isdst = 0 і isdst = 1: скопіюйте wall, встановіть tm_isdst = isdst,
     *   викличте mktime; потім localtime_r від результату і перевірте, що
     *   рік, місяць, день, година, хвилина І tm_isdst збіглися із запитаними.
     *   Якщо так — це справжній розв'язок. Однакові розв'язки не дублюйте,
     *   результати впорядкуйте за зростанням. */
    struct tm t = *wall;
    t.tm_isdst = -1;
    out[0] = mktime(&t);
    return 1;
}

static int cmd_conv(int argc, char **argv)
{
    struct tm wall;
    if (parse_wall(argv[0], &wall) != 0) return 2;
    time_t sol[2];
    int n = wall_to_utc(argv[1], &wall, sol);
    if (n < 0) return 2;
    printf("wall %s %s\n", argv[0], argv[1]);
    printf("status %s %d\n", n == 1 ? "ok" : n == 0 ? "gap" : "ambiguous", n);
    for (int i = 0; i < n; i++) print_utc(sol[i]);
    for (int z = 2; z < argc; z++) {
        if (use_zone(argv[z]) != 0) return 2;
        for (int i = 0; i < n; i++) {
            struct tm l;
            char b[64];
            localtime_r(&sol[i], &l);
            strftime(b, sizeof b, "%Y-%m-%d %H:%M %Z %z", &l);
            printf("in %s %s\n", argv[z], b);
        }
    }
    return 0;
}

static int cmd_now(int argc, char **argv)
{
    time_t t = time(NULL);
    for (int z = 0; z < argc; z++) {
        if (use_zone(argv[z]) != 0) return 2;
        struct tm l;
        char b[64];
        localtime_r(&t, &l);
        strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S %Z %z", &l);
        printf("now %s %s\n", argv[z], b);
    }
    return 0;
}

/* Щоденна подія: та сама година на годиннику ЗОНИ — це не кожні 86 400 секунд. */
static int cmd_daily(char **argv)
{
    int hh, mm;
    if (sscanf(argv[0], "%d:%d", &hh, &mm) != 2) { fprintf(stderr, "whenis: очікувалося ГГ:ХХ\n"); return 2; }
    struct tm day;
    char buf[40];
    snprintf(buf, sizeof buf, "%s 00:00", argv[2]);
    if (parse_wall(buf, &day) != 0) return 2;
    int days = atoi(argv[3]);
    time_t prev = 0;
    for (int d = 0; d < days; d++) {
        struct tm wall = day;
        wall.tm_mday += d;            /* календарна арифметика: нормалізує timegm */
        wall.tm_hour = 0; wall.tm_min = 0; wall.tm_sec = 0;
        time_t norm = timegm(&wall);  /* лише щоб отримати правильну дату */
        gmtime_r(&norm, &wall);
        wall.tm_hour = hh; wall.tm_min = mm; wall.tm_sec = 0;
        char ds[16];
        strftime(ds, sizeof ds, "%Y-%m-%d", &wall);
        time_t sol[2];
        int n = wall_to_utc(argv[1], &wall, sol);
        if (n < 0) return 2;
        if (n == 0) { printf("day %s gap\n", ds); continue; }
        struct tm u;
        char ub[32];
        gmtime_r(&sol[0], &u);
        strftime(ub, sizeof ub, "%Y-%m-%dT%H:%M:%SZ", &u);
        if (prev) printf("day %s utc %s hours_since_prev %.1f\n", ds, ub, (double)(sol[0] - prev) / 3600.0);
        else      printf("day %s utc %s\n", ds, ub);
        prev = sol[0];
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "now") == 0) return cmd_now(argc - 2, argv + 2);
    if (argc >= 4 && strcmp(argv[1], "conv") == 0) return cmd_conv(argc - 2, argv + 2);
    if (argc == 6 && strcmp(argv[1], "daily") == 0) return cmd_daily(argv + 2);
    fprintf(stderr, "usage: whenis now ЗОНА… | conv 'РРРР-ММ-ДД ГГ:ХХ' ЗОНА [ІНША…] |"
                    " daily ГГ:ХХ ЗОНА РРРР-ММ-ДД ДНІВ\n");
    return 2;
}
