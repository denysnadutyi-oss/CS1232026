#!/usr/bin/env python3
"""Детерміноване навантаження для kvcache (ЛР 6).

    python3 tools/kv-gen.py N [KEYS] > work.txt

N команд над KEYS різними ключами (за замовчуванням 1000): приблизно
40 % SET, 45 % GET, 12 % DEL, 2 % HISTORY і 1 % помилкових рядків.
Останній рядок — STATS. Однакові N і KEYS дають однаковий файл у всіх.
"""
import sys

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
n = int(sys.argv[1])
keys = int(sys.argv[2]) if len(sys.argv) == 3 else 1000

state = 2463534242


def rnd(m):
    """xorshift32: свій генератор, щоб файл не залежав від версії Python."""
    global state
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= state >> 17
    state ^= (state << 5) & 0xFFFFFFFF
    return state % m


ALPHA = "abcdefghijklmnopqrstuvwxyz0123456789"
out = []
for _ in range(n):
    key = f"user:{rnd(keys)}"
    r = rnd(100)
    if r < 40:
        val = "".join(ALPHA[rnd(len(ALPHA))] for _ in range(16 + rnd(49)))
        if rnd(4) == 0:
            val += " " + str(rnd(100000))      # значення з пробілом
        out.append(f"SET {key} {val}")
    elif r < 85:
        out.append(f"GET {key}")
    elif r < 97:
        out.append(f"DEL {key}")
    elif r < 99:
        out.append("HISTORY")
    else:
        out.append(f"SET {key}")               # помилка: немає значення
out.append("STATS")
sys.stdout.write("\n".join(out) + "\n")
