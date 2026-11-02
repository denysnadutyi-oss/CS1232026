/* pwcheck — як система зберігає й перевіряє паролі (ЛР 7, частина 3).
 *
 *   pwcheck hash ПРЕФІКС [ВАРТІСТЬ]   хеш пароля зі stdin: $y$, $6$, $2b$, $1$ …
 *   pwcheck verify ХЕШ                перевірити пароль зі stdin; код 0 — збіг, 1 — ні
 *   pwcheck bench                     скільки коштує один хеш кожним методом
 *   pwcheck pam ІМ'Я [СЕРВІС]         перевірити пароль через PAM (за замовчуванням lab07)
 *
 * Пароль завжди читається зі stdin (перший рядок), ніколи з аргументів:
 * аргументи процесу видно всім у /proc/PID/cmdline і в ps.
 * Завдання позначено TODO 3 і TODO 4.
 */
#define _GNU_SOURCE
#include <crypt.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PWMAX 512

/* Прочитати рядок зі stdin без '\n'. Якщо stdin — термінал, вимкнути луну. */
static int read_secret(const char *prompt, char *buf, size_t n, int echo)
{
    struct termios old, quiet;
    int tty = isatty(STDIN_FILENO);
    if (tty) {
        fputs(prompt, stderr);
        if (!echo && tcgetattr(STDIN_FILENO, &old) == 0) {
            quiet = old;
            quiet.c_lflag &= ~(tcflag_t)ECHO;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
        } else {
            tty = 0;
        }
    }
    char *r = fgets(buf, (int)n, stdin);
    if (tty) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &old); fputc('\n', stderr); }
    if (!r) return -1;
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

/* Порівняння за сталий час: тривалість не залежить від того,
 * у якій позиції рядки вперше відрізняються. */
static int same(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char d = (unsigned char)(la != lb);
    size_t n = la < lb ? la : lb;
    for (size_t i = 0; i < n; i++)
        d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

/* ---------------- hash ---------------- */

static int do_hash(const char *prefix, unsigned long count)
{
    char pw[PWMAX];
    if (read_secret("Пароль: ", pw, sizeof pw, 0) != 0) return 2;
    char *salt = crypt_gensalt_ra(prefix, count, NULL, 0);
    if (!salt) { perror("crypt_gensalt"); return 2; }
    struct crypt_data cd;
    memset(&cd, 0, sizeof cd);
    char *h = crypt_r(pw, salt, &cd);
    explicit_bzero(pw, sizeof pw);
    free(salt);
    if (!h || h[0] == '*') { fprintf(stderr, "pwcheck: метод не підтримується\n"); return 2; }
    puts(h);
    explicit_bzero(&cd, sizeof cd);
    return 0;
}

/* ---------------- verify ---------------- */

/* Чи відповідає пароль pw збереженому хешу stored? 1 — так, 0 — ні, -1 — помилка. */
static int check_password(const char *pw, const char *stored)
{
    /* TODO 3. Обчисліть хеш пароля з тими самими методом, вартістю й сіллю,
     * що й у stored (для crypt це і є другий аргумент), і порівняйте з stored.
     *  — crypt не годиться: він повертає статичний буфер; беріть crypt_rn
     *    зі struct crypt_data, яку виділіть у купі (вона займає ~32 КіБ);
     *  — помилку libxcrypt повідомляє рядком, що починається з '*', або NULL;
     *  — порівнюйте функцією same(), а не strcmp: чому — питання 6;
     *  — перед поверненням затріть struct crypt_data (explicit_bzero). */
    (void)pw; (void)stored; (void)same;
    return -1;
}

static int do_verify(const char *stored)
{
    char pw[PWMAX];
    if (read_secret("Пароль: ", pw, sizeof pw, 0) != 0) return 2;
    int ok = check_password(pw, stored);
    explicit_bzero(pw, sizeof pw);
    if (ok < 0) { fprintf(stderr, "pwcheck: хеш не розпізнано\n"); return 2; }
    puts(ok ? "ok" : "fail");
    return ok ? 0 : 1;
}

/* ---------------- bench ---------------- */

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void bench_one(const char *prefix, unsigned long count)
{
    char *salt = crypt_gensalt_ra(prefix, count, NULL, 0);
    if (!salt) { printf("bench %s %lu unsupported\n", prefix, count); return; }
    struct crypt_data cd;
    memset(&cd, 0, sizeof cd);
    int n = 0;
    double t0 = now(), t;
    do {                               /* не менше 3 хешів і не менше 0,5 с */
        crypt_r("correct horse", salt, &cd);
        n++;
        t = now() - t0;
    } while (n < 3 || t < 0.5);
    double ms = t * 1000 / n;
    /* налаштування — частина рядка солі до самої солі: $2b$05$, $y$j9T$, rounds=… */
    char setting[64];
    snprintf(setting, sizeof setting, "%s", salt);
    char *end = strrchr(setting, '$');
    if (end) end[1] = '\0';
    printf("bench %s ms %.3f per_sec %.0f\n", setting, ms, 1000 / ms);
    free(salt);
}

static int do_bench(void)
{
    bench_one("$1$", 0);
    bench_one("$5$", 0);
    bench_one("$6$", 0);
    bench_one("$6$", 50000);
    bench_one("$2b$", 0);
    bench_one("$2b$", 12);
    bench_one("$y$", 0);
    return 0;
}

/* ---------------- pam ---------------- */

/* Функція розмови: PAM ставить запитання, програма відповідає.
 * Відповіді виділяються malloc — їх звільнить PAM. */
static int converse(int n, const struct pam_message **msg,
                    struct pam_response **resp, void *data)
{
    (void)data;
    /* TODO 4. Для кожного з n повідомлень:
     *  — PAM_PROMPT_ECHO_OFF (пароль) і PAM_PROMPT_ECHO_ON: прочитати відповідь
     *    read_secret (луну вимкнути лише для ECHO_OFF) і покласти strdup-копію
     *    в r[i].resp; локальний буфер затерти;
     *  — PAM_ERROR_MSG і PAM_TEXT_INFO: надрукувати msg у stderr;
     *  — інше або помилка читання: звільнити вже виділені відповіді (затерши
     *    їх) і повернути PAM_CONV_ERR.
     * Масив відповідей r виділяється calloc на n елементів; при успіху
     * записати його в *resp і повернути PAM_SUCCESS. */
    (void)n; (void)msg; (void)resp;
    return PAM_CONV_ERR;
}

static int do_pam(const char *user, const char *service)
{
    struct pam_conv conv = { converse, NULL };
    pam_handle_t *ph = NULL;
    int rc = pam_start(service, user, &conv, &ph);
    if (rc != PAM_SUCCESS) { fprintf(stderr, "pam_start: %s\n", pam_strerror(ph, rc)); return 2; }
    rc = pam_authenticate(ph, 0);
    if (rc == PAM_SUCCESS)
        rc = pam_acct_mgmt(ph, 0);
    printf("pam %s %s\n", rc == PAM_SUCCESS ? "ok" : "fail", pam_strerror(ph, rc));
    pam_end(ph, rc);
    return rc == PAM_SUCCESS ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && argc <= 4 && strcmp(argv[1], "hash") == 0)
        return do_hash(argv[2], argc == 4 ? strtoul(argv[3], NULL, 10) : 0);
    if (argc == 3 && strcmp(argv[1], "verify") == 0) return do_verify(argv[2]);
    if (argc == 2 && strcmp(argv[1], "bench") == 0) return do_bench();
    if (argc >= 3 && argc <= 4 && strcmp(argv[1], "pam") == 0)
        return do_pam(argv[2], argc == 4 ? argv[3] : "lab07");
    fprintf(stderr, "usage: pwcheck hash ПРЕФІКС [ВАРТІСТЬ] | verify ХЕШ | bench | pam ІМ'Я [СЕРВІС]\n");
    return 2;
}
