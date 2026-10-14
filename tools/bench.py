#!/usr/bin/env python3
"""bench.py — час виконання команди: медіана кількох запусків.

    python3 tools/bench.py [-r ПОВТОРІВ] -- КОМАНДА [АРГУМЕНТИ...]

Перший запуск — «розігрівний» (прогріває кеш файлів) і не враховується.
Вивід команди відкидається. Друкує медіану, мінімум і максимум у секундах:
саме розкид показує, чи можна вірити різниці між двома програмами.
"""
import statistics
import subprocess
import sys
import time


def main():
    args = sys.argv[1:]
    reps = 5
    if len(args) >= 2 and args[0] == "-r":
        reps = int(args[1])
        args = args[2:]
    if args and args[0] == "--":
        args = args[1:]
    if not args:
        sys.exit("usage: bench.py [-r N] -- COMMAND [ARGS...]")

    def once():
        t0 = time.perf_counter()
        rc = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
        dt = time.perf_counter() - t0
        if rc != 0:
            sys.exit(f"команда завершилася з кодом {rc}")
        return dt

    once()
    times = [once() for _ in range(reps)]
    med = statistics.median(times)
    print(f"median {med:.3f} s   min {min(times):.3f}   max {max(times):.3f}   ({reps} runs)")


if __name__ == "__main__":
    main()
