/* loc — локаль процесу: що вона змінює і де ламає програми (ЛР 8, частина 4).
 *
 *   loc show        локаль із середовища: категорії, кодування, числа, дати, сортування
 *   loc parse ФАЙЛ  прочитати налаштування «ключ = число» і надрукувати їх для машини
 *
 * Обидва режими спершу викликають setlocale(LC_ALL, ""), як будь-яка
 * локалізована програма: повідомлення й дати для людини — мовою користувача.
 *
 * Формат ФАЙЛА: рядки «ключ = число», порожні рядки й рядки з # пропускаються.
 * Число завжди записано з десятковою крапкою (3.5, 1e-3) — незалежно від мови
 * користувача, бо файл читає і пише програма, а не людина.
 * Вивід parse:      key ЗНАЧЕННЯ (з крапкою, %.17g) … і в кінці total СУМА
 * Помилка у файлі:  повідомлення «ФАЙЛ:РЯДОК: …» у stderr, код 2.
 */
#define _GNU_SOURCE
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

static const struct { int cat; const char *name; } CATS[] = {
    { LC_CTYPE, "LC_CTYPE" },       { LC_NUMERIC, "LC_NUMERIC" },
    { LC_TIME, "LC_TIME" },         { LC_COLLATE, "LC_COLLATE" },
    { LC_MONETARY, "LC_MONETARY" }, { LC_MESSAGES, "LC_MESSAGES" },
};

static const char *WORDS[] = { "яблуко", "ґанок", "їжак", "іній", "гора", "єнот", "абетка", "Zebra", "apple" };
#define NWORDS (sizeof WORDS / sizeof WORDS[0])

static int by_strcmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int by_strcoll(const void *a, const void *b)
{
    return strcoll(*(char *const *)a, *(char *const *)b);
}

static void print_words(const char *label, const char **w)
{
    printf("%s", label);
    for (size_t i = 0; i < NWORDS; i++) printf(" %s", w[i]);
    printf("\n");
}

static int cmd_show(void)
{
    for (size_t i = 0; i < sizeof CATS / sizeof CATS[0]; i++)
        printf("%s %s\n", CATS[i].name, setlocale(CATS[i].cat, NULL));
    printf("codeset %s\n", nl_langinfo(CODESET));
    struct lconv *lc = localeconv();
    printf("decimal_point '%s' thousands_sep '%s'\n", lc->decimal_point, lc->thousands_sep);
    printf("number %'.2f\n", 1234567.891);

    time_t t = 1792891800;                     /* 2026-10-25 01:30:00 UTC */
    struct tm u;
    gmtime_r(&t, &u);
    char b[128];
    strftime(b, sizeof b, "%c", &u);       printf("date_c %s\n", b);
    strftime(b, sizeof b, "%x", &u);       printf("date_x %s\n", b);
    strftime(b, sizeof b, "%A, %d %B %Y", &u); printf("date_long %s\n", b);

    const char *w[NWORDS];
    memcpy(w, WORDS, sizeof w);
    qsort(w, NWORDS, sizeof w[0], by_strcmp);  print_words("sort_strcmp", w);
    qsort(w, NWORDS, sizeof w[0], by_strcoll); print_words("sort_strcoll", w);

    const char *city = "Київ";
    size_t chars = mbstowcs(NULL, city, 0);
    printf("strlen %zu mbstowcs %zd\n", strlen(city), chars == (size_t)-1 ? (ssize_t)-1 : (ssize_t)chars);
    return 0;
}

/* Розібрати число в рядку s (без пробілів довкола). Число записано
 * з крапкою незалежно від локалі процесу. Повертає 0 і *out або −1,
 * якщо рядок не є числом повністю. */
static int parse_number(const char *s, double *out)
{
    /* TODO 4a. Зараз — як у багатьох реальних програмах: strtod, а залишок
     * рядка не перевіряється. strtod читає число за правилами LC_NUMERIC
     * поточної локалі, тож в українській локалі «3.5» стає 3, а «3,5» — 3.5.
     * Перепишіть: розбирайте за правилами локалі "C", не змінюючи локаль
     * процесу (newlocale + strtod_l; об'єкт локалі створіть один раз),
     * і повертайте −1, якщо після числа щось залишилося або числа немає. */
    *out = strtod(s, NULL);
    return 0;
}

/* Записати число v у buf так, щоб parse_number прочитав його назад без втрат. */
static void format_number(double v, char *buf, size_t n)
{
    /* TODO 4b. printf форматує число за LC_NUMERIC: в українській локалі
     * вийде «3,5», і наступна програма такого файла не прочитає.
     * Перемкніть локаль лише для цього потоку (uselocale) на "C",
     * відформатуйте і поверніть попередню. */
    snprintf(buf, n, "%.17g", v);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
    return s;
}

static int cmd_parse(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 2; }
    char line[1024], out[64];
    double total = 0;
    int lineno = 0, rc = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) { fprintf(stderr, "%s:%d: немає '='\n", path, lineno); rc = 2; break; }
        *eq = '\0';
        char *key = trim(s), *val = trim(eq + 1);
        double v;
        if (parse_number(val, &v) != 0) {
            fprintf(stderr, "%s:%d: '%s' — не число\n", path, lineno, val);
            rc = 2;
            break;
        }
        format_number(v, out, sizeof out);
        printf("%s %s\n", key, out);
        total += v;
    }
    fclose(f);
    if (rc == 0) {
        format_number(total, out, sizeof out);
        printf("total %s\n", out);
    }
    return rc;
}

int main(int argc, char **argv)
{
    if (!setlocale(LC_ALL, ""))
        fprintf(stderr, "loc: локалі із середовища немає в системі, працюю в \"C\"\n");
    if (argc == 2 && strcmp(argv[1], "show") == 0) return cmd_show();
    if (argc == 3 && strcmp(argv[1], "parse") == 0) return cmd_parse(argv[2]);
    fprintf(stderr, "usage: loc show | loc parse ФАЙЛ\n");
    return 2;
}
