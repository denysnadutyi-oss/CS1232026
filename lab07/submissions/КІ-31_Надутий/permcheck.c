/* permcheck — як ядро вирішує, чи можна відкрити файл (ЛР 7, частина 2).
 *
 *   permcheck decide MODE OWNER GROUP WANT UID GIDS
 *       передбачити рішення для одного випадку: allow або deny.
 *       MODE — st_mode у вісімковій (100644 — файл, 40755 — каталог),
 *       WANT — r, w, x або їх поєднання (rw), GIDS — ефективний GID і
 *       додаткові групи через кому (1000,27,100).
 *   permcheck scan КАТАЛОГ
 *       обійти дерево і для кожного об'єкта й кожного з r, w, x порівняти
 *       своє передбачення з відповіддю ядра (faccessat з AT_EACCESS).
 *
 * Завдання позначено TODO 2.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#define MAXG 1024
struct creds {
    uid_t uid;                 /* ефективний UID */
    int   ngid;                /* gid[0] — ефективний GID, далі додаткові */
    gid_t gid[MAXG];
};

enum { WANT_R = 4, WANT_W = 2, WANT_X = 1 };

static int in_groups(const struct creds *c, gid_t g)
{
    for (int i = 0; i < c->ngid; i++)
        if (c->gid[i] == g) return 1;
    return 0;
}

/* Чи дозволить ядро процесу з посвідченням c доступ want (сума WANT_*)
 * до об'єкта з режимом mode, власником owner і групою group?
 * Без ACL. Повертає 1 — дозволено, 0 — ні. */
static int decide(mode_t mode, uid_t owner, gid_t group, int want,
                  const struct creds *c)
{
    /* TODO 2. Повторіть рішення ядра (generic_permission у fs/namei.c):
     *  — ефективний UID 0: біти прав не діють, але виконати звичайний
     *    файл можна лише тоді, коли x є хоча б в одному класі;
     *  — інакше обирається рівно один клас: власник, якщо UID збігся;
     *    група, якщо група файла є серед груп процесу; решта — інші;
     *  — дозвіл, якщо в обраному класі є всі запитані біти. */
    (void)mode; (void)owner; (void)group; (void)want; (void)c;
    return 0;
}

/* ---------------- decide ---------------- */

static int parse_want(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        if (*s == 'r') w |= WANT_R;
        else if (*s == 'w') w |= WANT_W;
        else if (*s == 'x') w |= WANT_X;
        else return -1;
    }
    return w;
}

static int do_decide(char **a)
{
    struct creds c = { 0 };
    mode_t mode = (mode_t)strtoul(a[0], NULL, 8);
    uid_t owner = (uid_t)strtoul(a[1], NULL, 10);
    gid_t group = (gid_t)strtoul(a[2], NULL, 10);
    int want = parse_want(a[3]);
    if (want <= 0) { fprintf(stderr, "permcheck: WANT — r, w, x\n"); return 2; }
    c.uid = (uid_t)strtoul(a[4], NULL, 10);
    char *gl = strdup(a[5]);
    for (char *t = strtok(gl, ","); t && c.ngid < MAXG; t = strtok(NULL, ","))
        c.gid[c.ngid++] = (gid_t)strtoul(t, NULL, 10);
    free(gl);
    puts(decide(mode, owner, group, want, &c) ? "allow" : "deny");
    return 0;
}

/* ---------------- scan ---------------- */

static struct creds me;
static long checked, mismatch, with_acl, skipped;

static void load_creds(void)
{
    me.uid = geteuid();
    me.gid[0] = getegid();
    int n = getgroups(MAXG - 1, me.gid + 1);
    me.ngid = 1 + (n > 0 ? n : 0);
}

static int has_acl(const char *path)
{
    return lgetxattr(path, "system.posix_acl_access", NULL, 0) > 0;
}

/* Щоб дістатися до об'єкта, потрібне x на кожному каталозі шляху. */
static int can_traverse(const char *path)
{
    char buf[PATH_MAX];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = strrchr(buf, '/'); p; p = strrchr(buf, '/')) {
        *p = '\0';
        const char *dir = buf[0] ? buf : "/";
        struct stat st;
        if (stat(dir, &st) != 0) return 0;
        if (!decide(st.st_mode, st.st_uid, st.st_gid, WANT_X, &me)) return 0;
        if (!buf[0]) break;
    }
    return 1;
}

static int visit(const char *path, const struct stat *st, int flag, struct FTW *f)
{
    (void)f;
    if (flag == FTW_SL || flag == FTW_SLN || flag == FTW_NS) return 0;
    if (has_acl(path)) { with_acl++; return 0; }
    static const int wants[] = { WANT_R, WANT_W, WANT_X };
    static const char names[] = "rwx";
    int trav = can_traverse(path);
    for (int i = 0; i < 3; i++) {
        int mine = trav && decide(st->st_mode, st->st_uid, st->st_gid, wants[i], &me);
        errno = 0;
        int kern = faccessat(AT_FDCWD, path, wants[i], AT_EACCESS | AT_SYMLINK_NOFOLLOW) == 0;
        if (!kern && errno == EROFS) { skipped++; continue; }   /* файлова система лише для читання */
        checked++;
        if (mine != kern) {
            mismatch++;
            printf("mismatch %c %s mode %o owner %u group %u mine %s kernel %s\n",
                   names[i], path, (unsigned)st->st_mode, (unsigned)st->st_uid,
                   (unsigned)st->st_gid, mine ? "allow" : "deny", kern ? "allow" : "deny");
        }
    }
    return 0;
}

static int do_scan(const char *dir)
{
    char abs[PATH_MAX];
    if (!realpath(dir, abs)) { perror(dir); return 2; }
    load_creds();
    nftw(abs, visit, 64, FTW_PHYS | FTW_MOUNT);
    printf("scan %s checked %ld mismatch %ld acl %ld readonly %ld\n",
           abs, checked, mismatch, with_acl, skipped);
    return mismatch ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 8 && strcmp(argv[1], "decide") == 0) return do_decide(argv + 2);
    if (argc == 3 && strcmp(argv[1], "scan") == 0) return do_scan(argv[2]);
    fprintf(stderr, "usage: permcheck decide MODE OWNER GROUP WANT UID GIDS\n"
                    "       permcheck scan КАТАЛОГ\n");
    return 2;
}
