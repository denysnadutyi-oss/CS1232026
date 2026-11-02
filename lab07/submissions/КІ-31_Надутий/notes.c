/* notes — нотатки користувачів у спільному сховищі (ЛР 7, частина 4).
 *
 * Програма встановлюється з бітом SUID і власником lab7notes — окремим
 * службовим користувачем, а не root. Сховище /var/lib/lab07/notes.db
 * належить lab7notes і має права 0600: напряму його не прочитає ніхто,
 * крім власника. notes відкриває сховище з правами lab7notes, а все
 * інше робить уже з правами того, хто її запустив.
 *
 *   notes id              справжній, ефективний і збережений UID і GID
 *   notes add ТЕКСТ…      додати нотатку від свого імені
 *   notes list            показати свої нотатки
 *   notes export ФАЙЛ     записати свої нотатки у файл
 *
 * Завдання позначено TODO 5.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define STORE   "/var/lib/lab07/notes.db"
#define TEXTMAX 200

static void die(const char *what)
{
    fprintf(stderr, "notes: %s: %s\n", what, strerror(errno));
    exit(2);
}

/* Шлях до сховища. Змінну середовища NOTES_DB зручно задавати під час
 * налагодження, але програма з SUID її не бачить: secure_getenv
 * повертає NULL, коли процес отримав привілеї через execve. */
static const char *store_path(void)
{
    const char *p = secure_getenv("NOTES_DB");
    return p ? p : STORE;
}

/* Назавжди стати тим, хто запустив програму: справжній, ефективний
 * і збережений UID (і GID) мають дорівнювати справжньому.
 * Після повернення повернути привілеї не можна ніяк — переконайтеся в
 * цьому перевіркою і завершіть процес, якщо це не так. */
static void drop_privileges(void)
{
    /* TODO 5. Зробіть справжній, ефективний і збережений GID рівними
     * справжньому GID, потім те саме для UID (setresgid, setresuid; чому
     * саме в такому порядку — питання 9). Перевіряйте код повернення
     * кожного виклику. Потім переконайтеся, що повернути ефективний UID
     * і GID, з якими програма стартувала, неможливо, і завершіть процес
     * з кодом 2, якщо це не так. */
}

static void print_id(void)
{
    uid_t r, e, s;
    gid_t gr, ge, gs;
    getresuid(&r, &e, &s);
    getresgid(&gr, &ge, &gs);
    printf("uid %u %u %u\ngid %u %u %u\n", (unsigned)r, (unsigned)e, (unsigned)s,
           (unsigned)gr, (unsigned)ge, (unsigned)gs);
}

/* Один рядок сховища: UID, час, текст через табуляцію. Табуляції й
 * переведення рядка в тексті замінюються пробілами, щоб нотатка не
 * могла видати себе за чужу. */
static int cmd_add(FILE *db, int argc, char **argv)
{
    char text[TEXTMAX + 1] = "";
    size_t len = 0;
    for (int i = 0; i < argc && len < TEXTMAX; i++) {
        int n = snprintf(text + len, sizeof text - len, "%s%s", i ? " " : "", argv[i]);
        if (n < 0) break;
        len += (size_t)n;
        if (len > TEXTMAX) len = TEXTMAX;
    }
    for (char *p = text; *p; p++)
        if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    if (!text[0]) { fprintf(stderr, "notes: порожня нотатка\n"); return 2; }
    flock(fileno(db), LOCK_EX);
    fseek(db, 0, SEEK_END);
    fprintf(db, "%u\t%ld\t%s\n", (unsigned)getuid(), (long)time(NULL), text);
    fflush(db);
    flock(fileno(db), LOCK_UN);
    puts("OK");
    return 0;
}

/* Свої нотатки — у out. Повертає кількість. */
static int copy_own(FILE *db, FILE *out)
{
    char line[TEXTMAX + 64];
    unsigned me = (unsigned)getuid();
    int n = 0;
    flock(fileno(db), LOCK_SH);
    rewind(db);
    while (fgets(line, sizeof line, db)) {
        char *end;
        unsigned long uid = strtoul(line, &end, 10);
        if (*end != '\t' || uid != me) continue;
        char *text = strchr(end + 1, '\t');
        if (!text) continue;
        fputs(text + 1, out);
        n++;
    }
    flock(fileno(db), LOCK_UN);
    return n;
}

static int cmd_export(FILE *db, const char *path)
{
    /* Файл відкривається вже після drop_privileges: з правами користувача. */
    FILE *out = fopen(path, "w");
    if (!out) die(path);
    int n = copy_own(db, out);
    if (fclose(out) != 0) die(path);
    printf("exported %d\n", n);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: notes id | add ТЕКСТ… | list | export ФАЙЛ\n");
        return 2;
    }
    /* Єдина дія з правами lab7notes: відкрити сховище. */
    umask(077);
    int fd = open(store_path(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) die(store_path());
    drop_privileges();

    FILE *db = fdopen(fd, "r+");
    if (!db) die("fdopen");
    int rc;
    if (strcmp(argv[1], "id") == 0 && argc == 2) {
        print_id();
        rc = 0;
    } else if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        rc = cmd_add(db, argc - 2, argv + 2);
    } else if (strcmp(argv[1], "list") == 0 && argc == 2) {
        printf("notes %d\n", copy_own(db, stdout));
        rc = 0;
    } else if (strcmp(argv[1], "export") == 0 && argc == 3) {
        rc = cmd_export(db, argv[2]);
    } else {
        fprintf(stderr, "usage: notes id | add ТЕКСТ… | list | export ФАЙЛ\n");
        rc = 2;
    }
    fclose(db);
    return rc;
}
