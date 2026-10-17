/* frag — фрагментація купи і чому пам'ять не повертається ядру
 * (ЛР 6, частина 2).
 *
 *   frag [-n N] [-s SIZE] [-k K]
 *
 * Сценарій: виділити N блоків по SIZE байтів (за замовчуванням 100000 × 1000),
 * звільнити всі, крім кожного K-го (за замовчуванням K = 2), викликати
 * malloc_trim(0) і нарешті виділити нові блоки подвійного розміру на ту саму
 * кількість байтів, що звільнили. Після кожної фази — рядок
 *
 *   phase НАЗВА live_kb L heap_kb H free_kb F rss_kb R
 *
 * Завдання позначено TODO 2.
 */
#define _GNU_SOURCE
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct snap {
    long live_kb;    /* зайнято блоками, які програма ще не звільнила */
    long heap_kb;    /* розмір купи, взятої в ядра через brk */
    long free_kb;    /* вільні шматки всередині купи */
    long rss_kb;     /* скільки фізичної пам'яті справді займає процес */
};

/* VmRSS з /proc/self/status, у КіБ; -1, якщо не вдалося. */
static long read_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "re");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb;
}

/* TODO 2. Заповнити struct snap: перші три поля — з mallinfo2()
 * (див. man 3 mallinfo2: uordblks, arena, fordblks), rss_kb — read_rss_kb().
 * Усі числа — у кібібайтах. */
static struct snap take_snap(void)
{
    struct mallinfo2 mi = mallinfo2();

    struct snap s = {
        .live_kb = (long)(mi.uordblks / 1024),
        .heap_kb = (long)(mi.arena / 1024),
        .free_kb = (long)(mi.fordblks / 1024),
        .rss_kb = read_rss_kb()
    };

    return s;
}

static void phase(const char *name)
{
    struct snap s = take_snap();
    printf("phase %-8s live_kb %7ld heap_kb %7ld free_kb %7ld rss_kb %7ld\n",
           name, s.live_kb, s.heap_kb, s.free_kb, s.rss_kb);
    fflush(stdout);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { perror("malloc"); exit(1); }
    memset(p, 0xAB, n);            /* блок справді використовується */
    return p;
}

int main(int argc, char **argv)
{
    size_t n = 100000, size = 1000, k = 2;
    int opt;
    while ((opt = getopt(argc, argv, "n:s:k:")) != -1) {
        switch (opt) {
        case 'n': n = strtoul(optarg, NULL, 10); break;
        case 's': size = strtoul(optarg, NULL, 10); break;
        case 'k': k = strtoul(optarg, NULL, 10); break;
        default:
            fprintf(stderr, "usage: frag [-n N] [-s SIZE] [-k K]\n");
            return 2;
        }
    }
    if (n == 0 || size == 0 || k == 0) {
        fprintf(stderr, "usage: frag [-n N] [-s SIZE] [-k K]\n");
        return 2;
    }

    char **blk = xmalloc(n * sizeof *blk);
    phase("start");

    for (size_t i = 0; i < n; i++)
        blk[i] = xmalloc(size);
    phase("alloc");

    size_t freed = 0;
    for (size_t i = 0; i < n; i++)
        if (i % k != 0) { free(blk[i]); blk[i] = NULL; freed++; }
    phase("free");

    malloc_trim(0);
    phase("trim");

    /* Стільки ж байтів, скільки звільнили, але блоками вдвічі більшими. */
    size_t m = freed / 2;
    char **big = xmalloc((m ? m : 1) * sizeof *big);
    for (size_t i = 0; i < m; i++)
        big[i] = xmalloc(2 * size);
    phase("refill");

    for (size_t i = 0; i < m; i++) free(big[i]);
    for (size_t i = 0; i < n; i++) free(blk[i]);
    free(big);
    free(blk);
    phase("end");
    return 0;
}
