/* calc — калькулятор цілих виразів з відновленням після помилок через
 * setjmp/longjmp (ЛР 5, частина 5).
 *
 * Читає вирази з stdin по одному на рядок і друкує результат або
 * «error: опис». Помилка в рядку не зупиняє роботу.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 200

static jmp_buf on_error;
static const char *err_msg;
static const char *p;

static void fail(const char *msg)
{
    err_msg = msg;
    longjmp(on_error, 1);
}

static void skip_spaces(void)
{
    while (*p == ' ' || *p == '\t')
        p++;
}

static long long expr(int depth);

static long long factor(int depth)
{
    if (depth > MAX_DEPTH)
        fail("too deep");

    skip_spaces();

    if (*p == '(') {
        p++;
        long long v = expr(depth + 1);
        skip_spaces();

        if (*p != ')')
            fail("expected )");

        p++;
        return v;
    }

    if (*p == '-') {
        p++;
        long long v = factor(depth + 1);

        if (v == LLONG_MIN)
            fail("overflow");

        return -v;
    }

    if (!isdigit((unsigned char)*p))
        fail("syntax error");

    long long v = 0;

    while (isdigit((unsigned char)*p)) {
        long long digit = *p - '0';
        long long temp;

        if (__builtin_mul_overflow(v, 10LL, &temp))
            fail("overflow");

        if (__builtin_add_overflow(temp, digit, &v))
            fail("overflow");

        p++;
    }

    return v;
}

static long long term(int depth)
{
    long long v = factor(depth);

    for (;;) {
        skip_spaces();
        char op = *p;

        if (op != '*' && op != '/')
            return v;

        p++;
        long long r = factor(depth);

        if (op == '*') {
            if (__builtin_mul_overflow(v, r, &v))
                fail("overflow");
        } else {
            if (r == 0)
                fail("division by zero");

            if (v == LLONG_MIN && r == -1)
                fail("overflow");

            v /= r;
        }
    }
}

static long long expr(int depth)
{
    long long v = term(depth);

    for (;;) {
        skip_spaces();
        char op = *p;

        if (op != '+' && op != '-')
            return v;

        p++;
        long long r = term(depth);

        if (op == '+') {
            if (__builtin_add_overflow(v, r, &v))
                fail("overflow");
        } else {
            if (__builtin_sub_overflow(v, r, &v))
                fail("overflow");
        }
    }
}

int main(void)
{
    char buf[1024];
    volatile int lines = 0, errors = 0;
    char *volatile text = NULL;

    if (setjmp(on_error) != 0) {
        errors++;
        printf("error: %s\n", err_msg);
        free(text);
        text = NULL;
    }

    while (fgets(buf, sizeof buf, stdin)) {
        lines++;
        buf[strcspn(buf, "\n")] = '\0';

        text = strdup(buf);

        if (!text) {
            perror("calc");
            return 2;
        }

        p = text;
        long long v = expr(0);

        skip_spaces();

        if (*p != '\0')
            fail("trailing garbage");

        printf("%lld\n", v);
        free(text);
        text = NULL;
    }

    printf("lines=%d errors=%d\n", lines, errors);
    return errors ? 1 : 0;
}
