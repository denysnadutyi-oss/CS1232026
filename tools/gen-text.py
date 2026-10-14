#!/usr/bin/env python3
"""gen-text.py — детермінований текстовий корпус для ЛР 4.

    python3 tools/gen-text.py WORDS [SEED] > corpus.txt

Однакові WORDS і SEED дають побайтово однаковий файл на будь-якій машині,
тож результати вимірювань різних студентів можна порівнювати.
Частоти слів підкоряються закону Ціпфа, як у природній мові: кілька
слів трапляються дуже часто, а більшість — рідко.
"""
import random
import sys

VOCAB = 60000
SYLL = ["ka", "ro", "mi", "tu", "sel", "van", "dor", "ix", "po", "le", "zan",
        "qu", "ber", "tri", "no", "sa", "gel", "fu", "ha", "wy"]
CYR = ["ядро", "потік", "пам'ять", "сигнал", "процес", "драйвер", "модуль"]


def make_vocab(rng):
    words, seen = [], set()
    while len(words) < VOCAB:
        w = "".join(rng.choice(SYLL) for _ in range(rng.randint(1, 4)))
        if rng.random() < 0.02:
            w = rng.choice(CYR) + w
        if w not in seen:
            seen.add(w)
            words.append(w)
    return words


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: gen-text.py WORDS [SEED]")
    total = int(sys.argv[1])
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) == 3 else 2026)
    vocab = make_vocab(rng)
    # Ціпф: вага k-го слова ~ 1/k
    cum, acc = [], 0.0
    for k in range(1, VOCAB + 1):
        acc += 1.0 / k
        cum.append(acc)
    out = sys.stdout.buffer
    seps = [b" ", b" ", b" ", b" ", b", ", b". ", b"\n", b" - ", b"; "]
    left = total
    while left > 0:
        chunk = min(left, 10000)
        picked = rng.choices(vocab, cum_weights=cum, k=chunk)
        parts = []
        for w in picked:
            if rng.random() < 0.05:
                w = w.capitalize()
            parts.append(w.encode())
            parts.append(rng.choice(seps))
        out.write(b"".join(parts))
        left -= chunk
    out.write(b"\n")


if __name__ == "__main__":
    main()
