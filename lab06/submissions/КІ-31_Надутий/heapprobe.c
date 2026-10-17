/* heapprobe — як malloc бере пам'ять у ядра (ЛР 6, частина 1).
 *
 *   heapprobe sizes        скільки байтів насправді дає malloc і де лежить блок
 *   heapprobe brk          як рухається program break під час malloc і free
 *   heapprobe threshold    з якого розміру malloc іде до ядра по mmap
 *
 * Вивід — рядки «ключ значення…»: їх читає людина і розбирає тест.
 * Завдання позначено TODO 1.
 */
#define _GNU_SOURCE
#include <malloc.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char *brk0;                 /* program break до першого malloc */

/* Блок лежить у купі, якщо його адреса між початковим і поточним break. */
static int in_heap(const void *p)
{
    const char *c = p;
    return c >= brk0 && c < (char *)sbrk(0);
}

static long heap_kb(void)
{
    return (long)(((char *)sbrk(0) - brk0) / 1024);
}

/* ---------------- sizes ---------------- */

static void do_sizes(void)
{
    static const size_t req[] = { 0, 1, 8, 24, 25, 40, 100, 1000, 4096,
                                  100000, 131072, 1048576 };
    for (size_t i = 0; i < sizeof req / sizeof req[0]; i++) {
        char *p = malloc(req[i]);
        char *q = malloc(req[i]);
        if (!p || !q) { perror("malloc"); exit(1); }
        printf("size %zu usable %zu step %td where %s/%s\n", req[i],
               malloc_usable_size(p), q - p,
               in_heap(p) ? "heap" : "mmap", in_heap(q) ? "heap" : "mmap");
        /* Блоки навмисно не звільняємо: інакше наступна пара зайняла б
         * їхнє місце, і крок між сусідами перестав би бути кроком. */
    }
}

/* ---------------- brk ---------------- */

#define NB 1000
static void do_brk(void)
{
    static char *blk[NB];
    printf("brk start heap_kb %ld\n", heap_kb());
    char *one = malloc(1);
    printf("brk first_malloc heap_kb %ld\n", heap_kb());
    for (int i = 0; i < NB; i++) {
        blk[i] = malloc(1000);
        if (!blk[i]) { perror("malloc"); exit(1); }
        memset(blk[i], 'x', 1000);
    }
    printf("brk alloc_1000x1000 heap_kb %ld\n", heap_kb());
    for (int i = 0; i < NB - 1; i++)     /* усі, крім останнього */
        free(blk[i]);
    printf("brk free_all_but_last heap_kb %ld\n", heap_kb());
    free(blk[NB - 1]);
    printf("brk free_last heap_kb %ld\n", heap_kb());
    free(one);
}

/* ---------------- threshold ---------------- */

/* Куди піде запит, залежить від історії процесу: скільки вільного місця
 * лишилося на вершині купи, які великі блоки вже звільняли. Тому кожна
 * проба виконується у свіжому процесі: heapprobe запускає сам себе
 * з режимом probe, а дитина повертає код 10 (купа) або 11 (mmap). */
enum { WHERE_HEAP = 10, WHERE_MMAP = 11 };

static void *volatile sink;   /* через нього компілятор не викине malloc/free */

static int probe_child(size_t size, int prefree)
{
    if (prefree) {                 /* спершу звільнити блок 1 МіБ */
        sink = malloc(1u << 20);
        free(sink);
    }
    /* Вичерпати вільне місце на вершині купи (top): тоді запит size точно
     * не поміститься в купу, що вже є, і malloc піде до ядра — по brk
     * або по mmap. Саме цей вибір ми й вимірюємо. */
    char *first = malloc(16);
    size_t room = (size_t)((char *)sbrk(0) - first);
    if (room > 256) sink = malloc(room - 128);

    void *p = malloc(size);
    if (!p) return 1;
    return in_heap(p) ? WHERE_HEAP : WHERE_MMAP;
}

static int lands_in_mmap(size_t size, int prefree)
{
    char a1[32], a2[2] = { prefree ? '1' : '0', 0 };
    snprintf(a1, sizeof a1, "%zu", size);
    char *args[] = { "heapprobe", "probe", a1, a2, NULL };
    extern char **environ;
    pid_t pid;
    int st;
    if (posix_spawn(&pid, "/proc/self/exe", NULL, NULL, args, environ) != 0) {
        perror("posix_spawn"); exit(1);
    }
    if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st)) { perror("waitpid"); exit(1); }
    if (WEXITSTATUS(st) != WHERE_HEAP && WEXITSTATUS(st) != WHERE_MMAP) {
        fprintf(stderr, "heapprobe: probe %zu failed\n", size); exit(1);
    }
    return WEXITSTATUS(st) == WHERE_MMAP;
}

/* TODO 1. Найменший розмір запиту з [1, hi], який malloc обслуговує
 * через mmap, — двійковий пошук за допомогою lands_in_mmap().
 * Якщо навіть hi іде в купу, повернути 0. */
static size_t find_threshold(size_t hi, int prefree)
{
    if (hi == 0 || !lands_in_mmap(hi, prefree))
        return 0;

    size_t lo = 1;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (lands_in_mmap(mid, prefree))
            hi = mid;
        else
            lo = mid + 1;
    }

    return lo;
}

static void do_threshold(void)
{
    printf("threshold initial %zu\n", find_threshold(8u << 20, 0));
    printf("threshold after_free %zu\n", find_threshold(8u << 20, 1));
    printf("half_mib initial %s\n", lands_in_mmap(512u << 10, 0) ? "mmap" : "heap");
    printf("half_mib after_free %s\n", lands_in_mmap(512u << 10, 1) ? "mmap" : "heap");
}

int main(int argc, char **argv)
{
    brk0 = sbrk(0);                /* до першого malloc, навіть до printf */
    if (argc == 4 && strcmp(argv[1], "probe") == 0)
        return probe_child(strtoul(argv[2], NULL, 10), argv[3][0] == '1');
    if (argc != 2) {
        fprintf(stderr, "usage: heapprobe sizes|brk|threshold\n");
        return 2;
    }
    if (strcmp(argv[1], "sizes") == 0) do_sizes();
    else if (strcmp(argv[1], "brk") == 0) do_brk();
    else if (strcmp(argv[1], "threshold") == 0) do_threshold();
    else { fprintf(stderr, "usage: heapprobe sizes|brk|threshold\n"); return 2; }
    return 0;
}
