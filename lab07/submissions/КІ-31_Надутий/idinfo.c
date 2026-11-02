/* idinfo — хто такий цей процес для ядра (ЛР 7, частина 1).
 *
 *   idinfo            ідентичність поточного процесу
 *   idinfo ІМ'Я       групи користувача за базою облікових записів (NSS)
 *
 * Вивід:
 *   uid R E S          справжній, ефективний і збережений UID
 *   gid R E S          те саме для GID
 *   groups G1 G2 …     додаткові групи процесу (getgroups)
 *   user ІМ'Я UID GID HOME SHELL
 *   member G1(ім'я) G2(ім'я) …   усі групи користувача (getgrouplist)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Ім'я групи за GID у буфер out; якщо групи немає в базі — число. */
static void group_name(gid_t g, char *out, size_t n)
{
    struct group gr, *res = NULL;
    char buf[16384];
    if (getgrgid_r(g, &gr, buf, sizeof buf, &res) == 0 && res)
        snprintf(out, n, "%s", gr.gr_name);
    else
        snprintf(out, n, "%u", (unsigned)g);
}

static void print_self(void)
{
    uid_t r, e, s;
    gid_t gr, ge, gs;
    getresuid(&r, &e, &s);
    getresgid(&gr, &ge, &gs);
    printf("uid %u %u %u\n", (unsigned)r, (unsigned)e, (unsigned)s);
    printf("gid %u %u %u\n", (unsigned)gr, (unsigned)ge, (unsigned)gs);

    int n = getgroups(0, NULL);
    gid_t *g = malloc(sizeof *g * (n > 0 ? n : 1));
    if (!g) { perror("malloc"); exit(1); }
    n = getgroups(n, g);
    printf("groups");
    for (int i = 0; i < n; i++) printf(" %u", (unsigned)g[i]);
    printf("\n");
    free(g);
}

/* Усі групи користувача: первинна з passwd і додаткові з бази груп.
 * Повертає масив (звільняє викликач) і кількість через *count. */
static gid_t *user_groups(const char *user, gid_t primary, int *count)
{
    /* TODO 1. Зараз функція повертає лише первинну групу.
     * Отримайте повний список getgrouplist: якщо масиву замало, функція
     * повертає -1 і записує в *ngroups потрібний розмір — збільште масив
     * і повторіть. Не покладайтеся на сталий розмір масиву. */
    (void)user;
    gid_t *g = malloc(sizeof *g);
    if (!g) return NULL;
    g[0] = primary;
    *count = 1;
    return g;
}

static int print_user(const char *name)
{
    struct passwd pw, *res = NULL;
    char buf[16384];
    int rc = getpwnam_r(name, &pw, buf, sizeof buf, &res);
    if (rc != 0) { fprintf(stderr, "idinfo: getpwnam_r: %s\n", strerror(rc)); return 2; }
    if (!res) { fprintf(stderr, "idinfo: немає користувача %s\n", name); return 1; }
    printf("user %s %u %u %s %s\n", pw.pw_name, (unsigned)pw.pw_uid,
           (unsigned)pw.pw_gid, pw.pw_dir, pw.pw_shell);
    int n = 0;
    gid_t *g = user_groups(pw.pw_name, pw.pw_gid, &n);
    if (!g) { perror("user_groups"); return 2; }
    printf("member");
    for (int i = 0; i < n; i++) {
        char nm[256];
        group_name(g[i], nm, sizeof nm);
        printf(" %u(%s)", (unsigned)g[i], nm);
    }
    printf("\n");
    free(g);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2) { fprintf(stderr, "usage: idinfo [ІМ'Я]\n"); return 2; }
    if (argc == 2) return print_user(argv[1]);
    print_self();
    return 0;
}
