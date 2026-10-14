# Лабораторна робота № 6

**Купа і стек: алокація, фрагментація і пошук витоків пам'яті**

Повний текст роботи — у Google Classroom. Тут лише те, що потрібно для здачі.

## Що здається

```
lab06/submissions/ГРУПА_Прізвище/
├── Makefile        ← скопіювати з lab06/starter/ без змін
├── heapprobe.c     — TODO 1: поріг mmap двійковим пошуком
├── frag.c          — TODO 2: знімок mallinfo2 і VmRSS
├── stackprobe.c    — TODO 3: обробник SIGSEGV на альтернативному стеку
├── kvcache.c       — усі витоки виправлено (з коментарем до кожного), TODO 4, TODO 5
└── REPORT.md
```

`make` має зібрати `heapprobe`, `frag`, `stackprobe` і `kvcache` без жодного попередження з `-Wall -Wextra`.

## Специфікація kvcache

```
SET КЛЮЧ ЗНАЧЕННЯ   → OK                 (значення — решта рядка)
GET КЛЮЧ            → ЗНАЧЕННЯ або (nil)
DEL КЛЮЧ            → 1 або 0
HISTORY             → history К1 К2 …    (до 16 останніх ключів GET, новіші першими)
STATS               → keys N inuse_kb K  (uordblks з mallinfo2, КіБ)
інше                → error: usage
```

Потрібен Linux з glibc 2.33 або новішою (WSL 2, віртуальна машина або Docker з `ubuntu:24.04`;
macOS і Alpine не підходять), пакети `build-essential clang valgrind python3`.

## Перевірка й вимірювання

Команди виконуються з вашого каталогу `lab06/submissions/ГРУПА_Прізвище/`.

```bash
make
python3 ../../../tools/lab06-test.py .                 # має бути 10/10

./heapprobe threshold
for k in 2 4 16 64; do ./frag -k $k; done
make stackprobe-O0 stackprobe-noclash
./stackprobe; ./stackprobe-O0; ./stackprobe-noclash -f 4000000

python3 ../../../tools/kv-gen.py 20000  > w20k.txt
python3 ../../../tools/kv-gen.py 200000 > w200k.txt
valgrind --leak-check=full --show-leak-kinds=all ./kvcache < w20k.txt > /dev/null
gcc -std=c17 -O1 -fanalyzer -c kvcache.c -o /dev/null   # жодного попередження
```

Робота, у якій `kvcache` не проходить `t_kv_valgrind`, не зараховується за частину 4.

## Що має бути в REPORT.md

Середовище (`uname -r`, процесор, `nproc`, версії `gcc`, glibc, `valgrind`, `ulimit -s`) ·
вивід `lab06-test.py` · таблиці `frag` і `stackprobe` · зведена таблиця ·
відповіді на дев'ять питань для звіту · висновки.
