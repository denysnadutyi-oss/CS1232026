# ЛР 7. Користувачі, права, паролі і SUID. Ціль за замовчуванням збирає всі чотири програми.
CC      ?= gcc
CFLAGS  ?= -std=c17 -O2 -g -Wall -Wextra
PROGS    = idinfo permcheck pwcheck notes

.PHONY: all clean install
all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

# Частина 3: libxcrypt (crypt_r, crypt_gensalt) і Linux-PAM
pwcheck: pwcheck.c
	$(CC) $(CFLAGS) $< -o $@ -lcrypt -lpam

# Частини 3 і 4: встановити notes (SUID lab7notes) і pwcheck у /opt/lab07
install: notes pwcheck
	sudo sh ../../../tools/lab07-setup.sh .

clean:
	rm -f $(PROGS) *.o
