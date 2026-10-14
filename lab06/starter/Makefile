# ЛР 6. Купа і стек. Ціль за замовчуванням збирає всі чотири програми.
CC      ?= gcc
CFLAGS  ?= -std=c17 -O2 -g -Wall -Wextra
PROGS    = heapprobe frag stackprobe kvcache

.PHONY: all clean
all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

# Частина 3: той самий stackprobe без оптимізації і без захисту від stack clash
stackprobe-O0: stackprobe.c
	$(CC) -std=c17 -O0 -g -Wall -Wextra $< -o $@
stackprobe-noclash: stackprobe.c
	$(CC) $(CFLAGS) -fno-stack-clash-protection $< -o $@

# Частина 4: kvcache з AddressSanitizer (LeakSanitizer увімкнено в ньому)
kvcache-asan: kvcache.c
	$(CC) -std=c17 -O1 -g -fno-omit-frame-pointer -fsanitize=address $< -o $@

clean:
	rm -f $(PROGS) stackprobe-O0 stackprobe-noclash kvcache-asan massif.out.* *.o
