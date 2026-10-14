/* stackprobe — де закінчується стек (ЛР 6, частина 3).
 *
 *   stackprobe [-f FRAME] [-n]
 *
 * Рекурсія без кінця: у кожному кадрі — масив із FRAME байтів (за
 * замовчуванням 0). Коли стек вичерпано, ядро надсилає SIGSEGV. Обробник,
 * що працює на окремому стеку, друкує один рядок і завершує процес з кодом 0:
 *
 *   overflow depth D used_kb U frame_bytes B limit_kb L
 *
 *   D — глибина рекурсії, на якій стався збій;
 *   U — від вершини стека main до адреси збою, КіБ;
 *   B — U / D: скільки байтів стека займає один виклик;
 *   L — м'яке обмеження RLIMIT_STACK (ulimit -s), КіБ.
 *
 * -n — не встановлювати обробник і подивитися, як процес завершиться без нього.
 * Завдання позначено TODO 3.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static size_t frame;               /* -f */
static long max_depth = 100000000; /* недосяжна межа: без неї GCC попереджає
                                       про нескінченну рекурсію */
static volatile long depth;        /* поточна глибина: її читає обробник */
static char *stack_top;            /* адреса в кадрі main */
static long limit_kb;

/* --- друк з обробника сигналу: лише write, жодного printf --- */

static void put_str(const char *s)
{
    if (write(STDOUT_FILENO, s, strlen(s)) < 0) { /* нічого не вдієш */ }
}

static void put_num(long v)
{
    char buf[24];
    int i = sizeof buf;
    int neg = v < 0;
    unsigned long u = neg ? 0UL - (unsigned long)v : (unsigned long)v;
    do { buf[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) buf[--i] = '-';
    if (write(STDOUT_FILENO, buf + i, sizeof buf - (size_t)i) < 0) { }
}

static void on_segv(int sig, siginfo_t *si, void *ctx)
{
    (void)sig; (void)ctx;
    long used = (long)(stack_top - (char *)si->si_addr);
    long d = depth;
    put_str("overflow depth ");     put_num(d);
    put_str(" used_kb ");           put_num(used / 1024);
    put_str(" frame_bytes ");       put_num(d ? used / d : 0);
    put_str(" limit_kb ");          put_num(limit_kb);
    put_str("\n");
    _exit(0);
}

/* TODO 3. Встановити on_segv обробником SIGSEGV так, щоб він виконувався
 * на окремому стеку: виділити пам'ять під альтернативний стек (64 КіБ —
 * із запасом), зареєструвати її через sigaltstack і викликати sigaction
 * з прапорцями SA_SIGINFO | SA_ONSTACK.
 * Помилки — perror і exit(1). */
static void install_handler(void)
{
}

static long recurse(long d)
{
    char buf[frame + 1];           /* масив змінної довжини — теж на стеку */
    memset(buf, (int)d, sizeof buf);
    depth = d;
    if (d >= max_depth) return 0;
    long r = recurse(d + 1);       /* не останній оператор: кадр потрібен далі */
    return r + buf[frame / 2];
}

int main(int argc, char **argv)
{
    char here;
    stack_top = &here;

    int handler = 1, opt;
    while ((opt = getopt(argc, argv, "f:n")) != -1) {
        switch (opt) {
        case 'f': frame = strtoul(optarg, NULL, 10); break;
        case 'n': handler = 0; break;
        default:
            fprintf(stderr, "usage: stackprobe [-f FRAME] [-n]\n");
            return 2;
        }
    }

    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0)
        limit_kb = rl.rlim_cur == RLIM_INFINITY ? -1 : (long)(rl.rlim_cur / 1024);

    if (handler) install_handler();
    long r = recurse(1);
    printf("no overflow (%ld)\n", r);  /* сюди не дійдемо */
    return 1;
}
