/* kvcache — сховище «ключ → значення», яке працює годинами (ЛР 6, частина 4).
 *
 * Читає команди зі stdin, по одній на рядок, і відповідає в stdout:
 *
 *   SET КЛЮЧ ЗНАЧЕННЯ   → OK              (значення — решта рядка, з пробілами)
 *   GET КЛЮЧ            → ЗНАЧЕННЯ або (nil)
 *   DEL КЛЮЧ            → 1, якщо ключ був, інакше 0
 *   HISTORY             → history К1 К2 …  (до 16 останніх ключів GET, новіші першими)
 *   STATS               → keys N inuse_kb K
 *   інше                → error: usage
 *
 * Програма працює правильно: тест функцій проходить. Але пам'ять вона
 * використовує неправильно — у ній кілька витоків різного роду. Знайдіть
 * їх інструментами з частини 4 і виправте. Завдання позначено TODO 4 і TODO 5.
 */
#define _GNU_SOURCE
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIST_SHOW 16

struct entry {
    char *key;
    char *val;
    struct entry *next;
};

struct kv {
    struct entry **bucket;
    size_t nbucket;
    size_t count;
};

/* Кільцевий буфер останніх HIST_SHOW ключів GET. */
static char *history[HIST_SHOW];
static size_t history_next = 0;
static size_t history_count = 0;

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { perror("kvcache"); exit(1); }
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) { perror("kvcache"); exit(1); }
    return p;
}

static uint64_t fnv1a(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
    return h;
}

/* ---------------- таблиця ---------------- */

static void kv_init(struct kv *t, size_t nbucket)
{
    t->bucket = calloc(nbucket, sizeof *t->bucket);
    if (!t->bucket) { perror("kvcache"); exit(1); }
    t->nbucket = nbucket;
    t->count = 0;
}

static struct entry **kv_slot(struct kv *t, const char *key)
{
    struct entry **pp = &t->bucket[fnv1a(key) % t->nbucket];
    while (*pp && strcmp((*pp)->key, key) != 0)
        pp = &(*pp)->next;
    return pp;                     /* *pp — запис з ключем або NULL */
}

/* Ключ і значення копіюються: виклик не передає їх у власність таблиці. */
static void kv_set(struct kv *t, const char *key, const char *val)
{
    struct entry **pp = kv_slot(t, key);
    if (*pp) {
        struct entry *e = *pp;
        char *new_val = xstrdup(val);

        // Шаблон витоку: перезапис вказівника без звільнення старого блока.
        free(e->val);
        e->val = new_val;
        return;
    }
    struct entry *e = xmalloc(sizeof *e);
    e->key = xstrdup(key);
    e->val = xstrdup(val);
    e->next = NULL;
    *pp = e;
    t->count++;
}

static const char *kv_get(struct kv *t, const char *key)
{
    struct entry *e = *kv_slot(t, key);
    return e ? e->val : NULL;
}

static int kv_del(struct kv *t, const char *key)
{
    struct entry **pp = kv_slot(t, key);
    struct entry *e = *pp;
    if (!e) return 0;
    *pp = e->next;
    // Шаблон витоку: звільнення структури без її динамічних полів.
    free(e->key);
    free(e->val);
    free(e);
    t->count--;
    return 1;
}

/* TODO 4. Звільнити всю пам'ять таблиці. */
static void kv_destroy(struct kv *t)
{
    // Шаблон витоку: знищення контейнера без звільнення його елементів.
    for (size_t i = 0; i < t->nbucket; i++) {
        struct entry *e = t->bucket[i];
        while (e) {
            struct entry *next = e->next;
            free(e->key);
            free(e->val);
            free(e);
            e = next;
        }
    }

    free(t->bucket);
    t->bucket = NULL;
    t->nbucket = t->count = 0;
}

/* ---------------- історія ---------------- */

/* TODO 5. Обмежена історія GET: кільцевий буфер на HIST_SHOW ключів. */
static void history_add(const char *key)
{
    // Шаблон витоку: необмежений ріст списку історії.
    char *copy = xstrdup(key);

    free(history[history_next]);
    history[history_next] = copy;

    history_next = (history_next + 1) % HIST_SHOW;

    if (history_count < HIST_SHOW)
        history_count++;
}

static void history_print(void)
{
    fputs("history", stdout);

    for (size_t i = 0; i < history_count; i++) {
        size_t pos = (history_next + HIST_SHOW - 1 - i) % HIST_SHOW;
        printf(" %s", history[pos]);
    }

    putchar('\n');
}

static void history_destroy(void)
{
    // Шаблон витоку: завершення без звільнення історії.
    for (size_t i = 0; i < HIST_SHOW; i++) {
        free(history[i]);
        history[i] = NULL;
    }

    history_next = 0;
    history_count = 0;
}

/* ---------------- ввід ---------------- */

/* Прочитати рядок довільної довжини без '\n' у буфер *buf місткістю *cap,
 * який росте за потреби. NULL — кінець вводу або брак пам'яті. */
static char *read_line(FILE *f, char **buf, size_t *cap)
{
    size_t len = 0;
    int c;
    while ((c = getc(f)) != EOF && c != '\n') {
        if (len + 1 >= *cap) {
            size_t new_cap = *cap ? 2 * *cap : 64;

            // Шаблон витоку: перезапис єдиного вказівника при невдалому realloc.
            char *tmp = realloc(*buf, new_cap);
            if (!tmp) return NULL;

            *buf = tmp;
            *cap = new_cap;
        }
        (*buf)[len++] = (char)c;
    }
    if (c == EOF && len == 0) return NULL;
    if (!*buf) {                               /* порожній рядок на самому початку */
        *buf = xmalloc(1);
        *cap = 1;
    }
    (*buf)[len] = '\0';
    return *buf;
}

/* ---------------- команди ---------------- */

static void execute(struct kv *t, char *line)
{
    char *cmd = strtok(line, " ");
    if (!cmd) { puts("error: usage"); return; }

    if (strcmp(cmd, "SET") == 0) {
        char *k = strtok(NULL, " ");
        if (!k) { puts("error: usage"); return; }
        char *key = xstrdup(k);
        char *val = strtok(NULL, "");          /* решта рядка */
        if (!val) {
            // Шаблон витоку: ранній вихід без звільнення тимчасового ключа.
            free(key);
            puts("error: usage");
            return;
        }
        kv_set(t, key, val);
        free(key);
        puts("OK");
    } else if (strcmp(cmd, "GET") == 0) {
        char *key = strtok(NULL, " ");
        if (!key) { puts("error: usage"); return; }
        const char *v = kv_get(t, key);
        puts(v ? v : "(nil)");
        history_add(key);
    } else if (strcmp(cmd, "DEL") == 0) {
        char *key = strtok(NULL, " ");
        if (!key) { puts("error: usage"); return; }
        puts(kv_del(t, key) ? "1" : "0");
    } else if (strcmp(cmd, "HISTORY") == 0) {
        history_print();
    } else if (strcmp(cmd, "STATS") == 0) {
        struct mallinfo2 mi = mallinfo2();
        printf("keys %zu inuse_kb %zu\n", t->count, mi.uordblks / 1024);
    } else {
        puts("error: usage");
    }
}

int main(void)
{
    struct kv t;
    kv_init(&t, 1024);
    char *buf = NULL, *line;
    size_t cap = 0;
    while ((line = read_line(stdin, &buf, &cap)) != NULL)
        execute(&t, line);
    free(buf);
    kv_destroy(&t);
    history_destroy();
    return 0;
}
