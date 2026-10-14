# ЛР 5. Процеси. Ціль за замовчуванням збирає всі чотири програми.
CC      ?= gcc
CFLAGS  ?= -std=c17 -O2 -g -Wall -Wextra
PROGS    = selfinfo launch forkcost calc

.PHONY: all clean calc-variants
all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

# Частина 5: один і той самий calc.c трьома способами
calc-variants: calc.c
	gcc   -std=c17 -O0 -g -Wall -Wextra calc.c -o calc-gcc-O0
	gcc   -std=c17 -O2 -g -Wall -Wextra calc.c -o calc-gcc-O2
	clang -std=c17 -O2 -g -Wall -Wextra calc.c -o calc-clang-O2

clean:
	rm -f $(PROGS) calc-gcc-O0 calc-gcc-O2 calc-clang-O2 *.o
