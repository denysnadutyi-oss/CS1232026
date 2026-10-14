#!/usr/bin/env python3
"""Перевірка ЛР 6: heapprobe, frag, stackprobe і kvcache.

    python3 tools/lab06-test.py КАТАЛОГ        # каталог із зібраними програмами

Потрібні КАТАЛОГ/{heapprobe,frag,stackprobe,kvcache} (make) і valgrind.
Кожен тест друкує OK або FAIL з поясненням; наприкінці — N/M.
"""
import os
import resource
import shutil
import subprocess
import sys
import tempfile

D = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
TOOLS = os.path.dirname(os.path.abspath(__file__))
HEAP = os.path.join(D, "heapprobe")
FRAG = os.path.join(D, "frag")
STACK = os.path.join(D, "stackprobe")
KV = os.path.join(D, "kvcache")
TMP = tempfile.mkdtemp(prefix="lab06-")
results = []


def run(args, stdin=None, timeout=60, env=None, preexec_fn=None):
    return subprocess.run(args, input=stdin, capture_output=True, text=True,
                          timeout=timeout, env=env, preexec_fn=preexec_fn)


def fields(line):
    """'a 1 b 2' -> {'a': '1', 'b': '2'} для пар після першого слова."""
    w = line.split()
    return {w[i]: w[i + 1] for i in range(1, len(w) - 1, 2)}


def test(fn):
    name = fn.__name__
    try:
        msg = fn()
    except subprocess.TimeoutExpired:
        msg = "завис (timeout)"
    except Exception as e:  # noqa: BLE001
        msg = f"виняток {type(e).__name__}: {e}"
    ok = msg is None
    results.append(ok)
    print(f"{'OK  ' if ok else 'FAIL'} {name}" + ("" if ok else f": {msg}"))
    return fn


def workload(n):
    path = os.path.join(TMP, f"w{n}.txt")
    if not os.path.exists(path):
        with open(path, "w") as f:
            subprocess.run([sys.executable, os.path.join(TOOLS, "kv-gen.py"), str(n)],
                           stdout=f, check=True)
    with open(path) as f:
        return f.read()


# ---------------- частина 1: heapprobe ----------------

def thresholds(env=None):
    r = run([HEAP, "threshold"], env=env)
    t = {}
    for line in r.stdout.splitlines():
        w = line.split()
        if len(w) == 3 and w[0] == "threshold":
            t[w[1]] = int(w[2])
    return r, t


@test
def t_threshold():
    r, t = thresholds()
    if r.returncode != 0:
        return f"код {r.returncode}: {r.stderr.strip()[:200]}"
    a, b = t.get("initial", 0), t.get("after_free", 0)
    if not 128 * 1024 - 64 <= a <= 128 * 1024:
        return f"initial = {a}; очікувалося трохи менше за 131072 (поріг 128 КіБ мінус заголовок)"
    if not 1024 * 1024 <= b <= 1024 * 1024 + 8192:
        return f"after_free = {b}; очікувався поріг близько 1 МіБ після звільнення блока 1 МіБ"


@test
def t_threshold_tunable():
    env = dict(os.environ, GLIBC_TUNABLES="glibc.malloc.mmap_threshold=65536")
    r, t = thresholds(env)
    a, b = t.get("initial", 0), t.get("after_free", 0)
    if not 64 * 1024 - 64 <= a <= 64 * 1024:
        return f"з mmap_threshold=65536 initial = {a}, очікувалося трохи менше за 65536"
    if b != a:
        return f"явно заданий поріг не мав змінитися після free, а став {b}"


# ---------------- частина 2: frag ----------------

def frag(k, n=100000):
    r = run([FRAG, "-n", str(n), "-k", str(k)])
    ph = {}
    for line in r.stdout.splitlines():
        w = line.split()
        if len(w) >= 2 and w[0] == "phase":
            ph[w[1]] = {key: int(v) for key, v in fields(" ".join(w[1:])).items()}
    return r, ph


@test
def t_frag_snapshot():
    r, ph = frag(2)
    if r.returncode != 0 or not {"alloc", "free", "trim", "refill"} <= ph.keys():
        return f"код {r.returncode}, фази: {sorted(ph)}"
    a, f, rf = ph["alloc"], ph["free"], ph["refill"]
    expect_live = 100000 * 1008 // 1024
    if not 0.97 * expect_live <= a["live_kb"] <= 1.03 * expect_live:
        return f"alloc: live_kb {a['live_kb']}, очікувалося ≈ {expect_live} (100000 блоків по 1008 Б)"
    if not 0.45 * a["live_kb"] <= f["live_kb"] <= 0.55 * a["live_kb"]:
        return f"free: live_kb {f['live_kb']} — після звільнення половини блоків має бути ≈ половина"
    if a["heap_kb"] < a["live_kb"] or abs(f["heap_kb"] - (f["live_kb"] + f["free_kb"])) > 64:
        return "heap_kb має дорівнювати live_kb + free_kb (з точністю до заголовків)"
    if a["rss_kb"] < a["heap_kb"]:
        return f"rss_kb {a['rss_kb']} менше за купу {a['heap_kb']}: прочитано не VmRSS?"
    if rf["heap_kb"] < 1.3 * f["heap_kb"]:
        return "refill з K=2 мав збільшити купу: великі блоки не вміщаються в дірки"


@test
def t_frag_trim():
    r, ph = frag(16)
    if r.returncode != 0 or "trim" not in ph:
        return f"код {r.returncode}"
    before, after = ph["free"]["rss_kb"], ph["trim"]["rss_kb"]
    if not after < 0.6 * before:
        return f"K=16: rss_kb до trim {before}, після {after} — мало впасти принаймні на 40 %"
    r2, ph2 = frag(2)
    b2, a2 = ph2["free"]["rss_kb"], ph2["trim"]["rss_kb"]
    if a2 < 0.9 * b2:
        return f"K=2: rss_kb після trim впав з {b2} до {a2}, хоча дірки менші за сторінку"


# ---------------- частина 3: stackprobe ----------------

def stack(args, limit_kb=None):
    def pre():
        if limit_kb:
            resource.setrlimit(resource.RLIMIT_STACK, (limit_kb * 1024, resource.getrlimit(resource.RLIMIT_STACK)[1]))
    r = run([STACK] + args, preexec_fn=pre)
    line = next((ln for ln in r.stdout.splitlines() if ln.startswith("overflow")), "")
    return r, {k: int(v) for k, v in fields(line).items()}


@test
def t_stack_handler():
    r, s = stack([], limit_kb=8192)
    if r.returncode != 0 or "depth" not in s:
        return (f"код {r.returncode}: обробник не спрацював (без окремого стека йому ніде "
                "виконуватися)" if r.returncode < 0 else f"код {r.returncode}, вивід: {r.stdout[:120]!r}")
    if not 8192 - 256 <= s["used_kb"] <= 8192:
        return f"used_kb {s['used_kb']} при ulimit -s 8192"
    if not 16 <= s["frame_bytes"] <= 512:
        return f"frame_bytes {s['frame_bytes']} — неправдоподібний кадр для -f 0"


@test
def t_stack_limit():
    r, s = stack(["-f", "1000"], limit_kb=1024)
    if r.returncode != 0 or "depth" not in s:
        return f"код {r.returncode}"
    if s.get("limit_kb") != 1024 or not 1024 - 128 <= s["used_kb"] <= 1024:
        return f"при ulimit -s 1024: limit_kb {s.get('limit_kb')}, used_kb {s['used_kb']}"
    if not 1000 <= s["frame_bytes"] <= 1400:
        return f"frame_bytes {s['frame_bytes']} для -f 1000"


# ---------------- частина 4: kvcache ----------------

@test
def t_kv_func():
    inp = ("SET a 1\nSET b hello world\nGET a\nGET b\nGET c\nSET a 2\nGET a\n"
           "DEL b\nDEL b\nSET x\nSTATS\nFOO\n\n")
    exp = ["OK", "OK", "1", "hello world", "(nil)", "OK", "2", "1", "0",
           "error: usage", None, "error: usage", "error: usage"]
    r = run([KV], stdin=inp)
    got = r.stdout.splitlines()
    if r.returncode != 0 or len(got) != len(exp):
        return f"код {r.returncode}, рядків {len(got)} замість {len(exp)}"
    for i, (g, e) in enumerate(zip(got, exp)):
        if e is None:
            if not g.startswith("keys 1 inuse_kb "):
                return f"STATS: {g!r}"
        elif g != e:
            return f"рядок {i + 1}: {g!r} замість {e!r}"


@test
def t_kv_history():
    r = run([KV], stdin="HISTORY\n" + "".join(f"GET k{i}\n" for i in range(20)) + "HISTORY\n")
    got = [ln for ln in r.stdout.splitlines() if ln.startswith("history")]
    want = "history " + " ".join(f"k{i}" for i in range(19, 3, -1))
    if got != ["history", want]:
        return f"отримано {got}"


@test
def t_kv_valgrind():
    if not shutil.which("valgrind"):
        return "valgrind не встановлено"
    r = run(["valgrind", "--leak-check=full", "--show-leak-kinds=all",
             "--errors-for-leak-kinds=all", "--error-exitcode=9", "-q", KV],
            stdin=workload(20000), timeout=300)
    if r.returncode != 0:
        tail = [ln for ln in r.stderr.splitlines() if "lost" in ln or "reachable" in ln]
        return f"код {r.returncode}: " + "; ".join(x.split("==", 2)[-1].strip() for x in tail[:3])


@test
def t_kv_growth():
    def inuse(n):
        r = run([KV], stdin=workload(n), timeout=120)
        w = r.stdout.splitlines()[-1].split()       # keys N inuse_kb K
        return int(dict(zip(w[0::2], w[1::2]))["inuse_kb"])
    small, big = inuse(20000), inuse(200000)
    if big - small > 64:
        return (f"inuse_kb після 20 000 команд — {small}, після 200 000 — {big}: "
                "на тому самому наборі ключів пам'ять не має рости")


shutil.rmtree(TMP, ignore_errors=True)
print(f"\n{sum(results)}/{len(results)}")
sys.exit(0 if all(results) else 1)
