#!/usr/bin/env python3
"""wordfreq-test.py — перевірка коректності wordfreq (ЛР 4).

    python3 tools/wordfreq-test.py ./wordfreq
    python3 tools/wordfreq-test.py ./wordfreq-asan      # те саме під санітайзерами

Кожен тест запускає програму як окремий процес і порівнює stdout, stderr
і код повернення зі специфікацією. Еталонний результат рахує сам скрипт,
тому тест не залежить від того, яку таблицю (naive чи hash) ви зібрали.
"""
import os
import re
import subprocess
import sys
import tempfile

PROG = None
TMP = None


def ref_counts(data: bytes, n=10):
    counts = {}
    word = bytearray()

    def flush():
        if word:
            w = bytes(word)
            counts[w] = counts.get(w, 0) + 1
            word.clear()

    for c in data:
        if 65 <= c <= 90:
            word.append(c + 32)
        elif 97 <= c <= 122 or c >= 0x80:
            word.append(c)
        else:
            flush()
    flush()
    items = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:n]
    return b"".join(b"%d %s\n" % (c, w) for w, c in items)


def run(args, stdin=b"", stdout=None):
    p = subprocess.run([PROG] + args, input=stdin,
                       stdout=stdout if stdout is not None else subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=120)
    m = sanitizer_noise(p.stderr)
    if m:
        start = max(0, p.stderr.rfind(b"\n", 0, m.start()) + 1)
        raise AssertionError("санітайзер повідомив про помилку:\n    "
                             + p.stderr[start:start + 400].decode(errors="replace"))
    return p.returncode, (p.stdout or b""), p.stderr


def tmpfile(name, data: bytes):
    path = os.path.join(TMP, name)
    with open(path, "wb") as f:
        f.write(data)
    return path


def sanitizer_noise(err: bytes):
    return re.search(rb"(AddressSanitizer|LeakSanitizer|runtime error:)", err)


# ------------------------------------------------------------------ tests

def t_basic():
    text = b"The cat and the hat. THE end, and the cat!\n"
    f = tmpfile("basic.txt", text)
    rc, out, err = run([f])
    assert rc == 0, f"код {rc}, очікувався 0; stderr: {err!r}"
    assert out == ref_counts(text), f"stdout:\n{out.decode(errors='replace')}"


def t_ties_and_n():
    text = b"b a c b a c d e f g h i j k l m n\n"
    f = tmpfile("ties.txt", text)
    rc, out, _ = run(["-n", "3", f])
    assert rc == 0 and out == b"2 a\n2 b\n2 c\n", f"отримано:\n{out.decode()}"
    rc, out, _ = run(["-n", "100", f])
    assert out == ref_counts(text, 100), "-n більше за кількість слів"


def t_stdin():
    text = b"alpha beta alpha\ngamma alpha beta\n"
    rc, out, err = run([], stdin=text)
    assert rc == 0, f"код {rc}; stderr: {err!r}"
    assert out == ref_counts(text), f"stdout:\n{out.decode()}"


def t_bad_n():
    f = tmpfile("x.txt", b"x\n")
    for bad in ["0", "-3", "abc", "5x", "", "99999999999999999999"]:
        rc, out, err = run(["-n", bad, f])
        assert rc == 2, f"-n '{bad}': код {rc}, очікувався 2"
        assert out == b"", f"-n '{bad}': при помилці аргументів нічого не друкувати в stdout"
        assert b"usage" in err, f"-n '{bad}': у stderr немає підказки usage"
    rc, _, _ = run(["-n"])
    assert rc == 2, f"-n без значення: код {rc}, очікувався 2"
    rc, _, _ = run(["-x", f])
    assert rc == 2, f"невідомий ключ -x: код {rc}, очікувався 2"


def t_missing():
    missing = os.path.join(TMP, "no-such-file.txt")
    rc, out, err = run([missing])
    assert rc == 1, f"код {rc}, очікувався 1"
    want = f"wordfreq: {missing}: No such file or directory\n".encode()
    assert err.endswith(want) or want in err, f"stderr: {err!r}\nочікувалось: {want!r}"


def t_directory():
    rc, out, err = run([TMP])
    assert rc == 1, f"каталог як файл: код {rc}, очікувався 1"
    assert b"Is a directory" in err, f"stderr: {err!r} (open для каталогу вдається, помилку дає read)"


def t_permission():
    if os.geteuid() == 0:
        return "SKIP (запущено від root: права не перевіряються)"
    f = tmpfile("secret.txt", b"secret\n")
    os.chmod(f, 0)
    rc, out, err = run([f])
    os.chmod(f, 0o600)
    assert rc == 1, f"код {rc}, очікувався 1"
    assert b"Permission denied" in err, f"stderr: {err!r}"


def t_partial():
    good1 = tmpfile("good1.txt", b"one two two\n")
    good2 = tmpfile("good2.txt", b"two three\n")
    bad = os.path.join(TMP, "absent.txt")
    rc, out, err = run([good1, bad, good2])
    assert rc == 1, f"код {rc}, очікувався 1"
    assert out == ref_counts(b"one two two\ntwo three\n"), \
        "помилка одного файла не повинна зупиняти обробку інших"
    assert err.count(b"\n") >= 1 and b"absent.txt" in err


def t_devfull():
    if not os.path.exists("/dev/full"):
        return "SKIP (немає /dev/full)"
    f = tmpfile("small.txt", b"to be or not to be\n")
    with open("/dev/full", "wb") as full:
        rc, _, err = run([f], stdout=full)
    assert rc == 1, f"вивід у /dev/full: код {rc}, очікувався 1 (помилку запису не перевірено)"
    assert b"wordfreq: stdout: No space left on device" in err, f"stderr: {err!r}"


def t_boundaries():
    # Слова навмисно перетинають межі 4 КіБ і 64 КіБ, де закінчується read().
    unit = b"x" * 4093 + b" boundary" + b"y" * 70000 + b"\n"
    text = unit * 3 + b"Z" * 65536 + b" end"
    f = tmpfile("bound.txt", text)
    rc, out, err = run(["-n", "20", f])
    assert rc == 0, f"код {rc}; stderr: {err[:300]!r}"
    assert out == ref_counts(text, 20), "слово, розрізане між двома read(), пораховано неправильно"


def t_utf8():
    text = "Ядро ядро kernel Kernel модуль\n".encode()
    f = tmpfile("utf8.txt", text)
    rc, out, _ = run([f])
    assert rc == 0 and out == ref_counts(text), f"stdout:\n{out.decode(errors='replace')}"


def t_corpus():
    here = os.path.dirname(os.path.abspath(__file__))
    gen = subprocess.run([sys.executable, os.path.join(here, "gen-text.py"), "200000", "7"],
                         stdout=subprocess.PIPE, check=True).stdout
    f = tmpfile("corpus.txt", gen)
    rc, out, err = run(["-n", "50", f, f])
    assert rc == 0, f"код {rc}; stderr: {err[:300]!r}"
    assert out == ref_counts(gen + gen, 50), "результат на корпусі не збігається з еталоном"


TESTS = [t_basic, t_ties_and_n, t_stdin, t_bad_n, t_missing, t_directory,
         t_permission, t_partial, t_devfull, t_boundaries, t_utf8, t_corpus]


def main():
    global PROG, TMP
    if len(sys.argv) != 2:
        sys.exit("usage: wordfreq-test.py ./wordfreq")
    PROG = os.path.abspath(sys.argv[1])
    if not os.access(PROG, os.X_OK):
        sys.exit(f"{PROG}: не знайдено або не виконуваний файл")
    passed = total = 0
    with tempfile.TemporaryDirectory() as d:
        TMP = d
        for t in TESTS:
            total += 1
            try:
                note = t()
                passed += 1
                print(f"PASS {t.__name__}" + (f"  {note}" if note else ""))
            except AssertionError as e:
                print(f"FAIL {t.__name__}: {e}")
            except subprocess.TimeoutExpired:
                print(f"FAIL {t.__name__}: перевищено час очікування")
    print(f"{passed}/{total}")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
