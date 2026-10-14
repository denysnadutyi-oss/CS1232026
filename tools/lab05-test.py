#!/usr/bin/env python3
"""Перевірка ЛР 5: launch (частини 2–3) і calc (частина 5).

    python3 tools/lab05-test.py КАТАЛОГ        # каталог із зібраними програмами

Потрібні КАТАЛОГ/launch, КАТАЛОГ/selfinfo, КАТАЛОГ/calc (make).
Кожен тест друкує OK або FAIL з поясненням; наприкінці — N/M.
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time

D = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
LAUNCH = os.path.join(D, "launch")
SELF = os.path.join(D, "selfinfo")
CALC = os.path.join(D, "calc")
TMP = tempfile.mkdtemp(prefix="lab05-")
results = []


def run(args, stdin=None, timeout=10, env=None, cwd=None):
    return subprocess.run(args, input=stdin, capture_output=True, text=True,
                          timeout=timeout, env=env, cwd=cwd, close_fds=True)


def report(text):
    """Розібрати звіт selfinfo у словник списків."""
    info = {}
    for line in text.splitlines():
        key, _, val = line.partition(" ")
        if key in ("fd", "env", "addr"):
            sub, _, rest = val.partition(" ") if key != "env" else val.partition("=")
            info.setdefault(key, {})[sub] = rest
        else:
            info[key] = val
    return info


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


def expect(cond, msg):
    if not cond:
        raise AssertionError(msg)


@test
def t_exit_code():
    r = run([LAUNCH, SELF, "-x", "3"])
    if r.returncode != 3:
        return f"код {r.returncode}, очікувався 3 (код дитини)"
    if "argv[0] " + SELF not in r.stdout:
        return "у stdout немає звіту selfinfo: дитина не запустилась або вивід втрачено"


@test
def t_argv():
    r = run([LAUNCH, "--", SELF, "a b", "", "-x"])
    i = report(r.stdout)
    if i.get("argc") != "4" or i.get("argv[1]") != "a b" or i.get("argv[2]") != "" or i.get("argv[3]") != "-x":
        return f"аргументи передано не точно: argc={i.get('argc')}, argv[1]={i.get('argv[1]')!r}"


@test
def t_env():
    env = dict(os.environ, KEEP="1", DROP="x")
    r = run([LAUNCH, "-e", "FOO=bar baz", "-e", "EMPTY=", "-e", "KEEP=2", "-u", "DROP", SELF], env=env)
    e = report(r.stdout).get("env", {})
    if e.get("FOO") != "bar baz" or e.get("EMPTY") != "" or e.get("KEEP") != "2":
        return f"-e не спрацював: FOO={e.get('FOO')!r} EMPTY={e.get('EMPTY')!r} KEEP={e.get('KEEP')!r}"
    if "DROP" in e:
        return "-u DROP: змінна лишилася в оточенні дитини"
    if "PATH" not in e:
        return "оточення дитини не успадковано (немає PATH)"


@test
def t_path_search():
    r = run([LAUNCH, "true"])
    if r.returncode != 0:
        return f"`launch true` → код {r.returncode}: пошук у PATH не працює (execvp?)"


@test
def t_not_found():
    r = run([LAUNCH, "no-such-program-lab05"])
    if r.returncode != 127:
        return f"код {r.returncode}, очікувався 127"
    want = "launch: no-such-program-lab05: No such file or directory"
    if want not in r.stderr:
        return f"stderr {r.stderr.strip()!r}, очікувалося {want!r}"


@test
def t_not_executable():
    path = os.path.join(TMP, "plain.txt")
    with open(path, "w") as f:
        f.write("not a program\n")
    os.chmod(path, 0o644)
    r = run([LAUNCH, path])
    if r.returncode != 126:
        return f"код {r.returncode}, очікувався 126"
    if f"launch: {path}: Permission denied" not in r.stderr:
        return f"stderr {r.stderr.strip()!r}"


@test
def t_signal():
    r = run([LAUNCH, SELF, "-k", "15"])
    if r.returncode != 128 + 15:
        return f"код {r.returncode}, очікувався 143 (128 + SIGTERM)"
    if "killed by signal 15" not in r.stderr:
        return f"stderr {r.stderr.strip()!r}: немає «killed by signal 15»"


@test
def t_output():
    out = os.path.join(TMP, "out.txt")
    r = run([LAUNCH, "-o", out, SELF])
    if r.stdout:
        return "-o: щось потрапило в stdout самого launch"
    with open(out) as f:
        i = report(f.read())
    if i.get("fd", {}).get("1") != out:
        return f"-o: fd 1 дитини → {i.get('fd', {}).get('1')!r}, очікувався {out}"
    bad = os.path.join(TMP, "no-dir", "x.txt")
    r = run([LAUNCH, "-o", bad, SELF])
    if r.returncode != 1 or f"launch: {bad}: No such file or directory" not in r.stderr:
        return f"-o в неіснуючий каталог: код {r.returncode}, stderr {r.stderr.strip()!r}"


@test
def t_chdir():
    r = run([LAUNCH, "-C", TMP, SELF])
    if report(r.stdout).get("cwd") != os.path.realpath(TMP):
        return "-C: робочий каталог дитини не змінився"
    r = run([LAUNCH, "-C", "/no-such-dir-lab05", SELF])
    if r.returncode != 1 or "launch: /no-such-dir-lab05: No such file or directory" not in r.stderr:
        return f"-C неіснуючий: код {r.returncode}, stderr {r.stderr.strip()!r}"


@test
def t_no_fd_leak():
    log = os.path.join(TMP, "leak.log")
    out = os.path.join(TMP, "leak.out")
    run([LAUNCH, "-l", log, "-o", out, SELF])
    with open(out) as f:
        fds = report(f.read()).get("fd", {})
    extra = {k: v for k, v in fds.items() if k not in ("0", "1", "2")}
    if extra:
        return f"дитина успадкувала зайві дескриптори: {extra}"


@test
def t_log():
    log = os.path.join(TMP, "run.log")
    if os.path.exists(log):
        os.unlink(log)
    r = run([LAUNCH, "-l", log, SELF, "-x", "5"])
    pid = report(r.stdout).get("pid")
    with open(log) as f:
        text = f.read()
    if f"start pid={pid} cmd={SELF}" not in text or f"exit pid={pid} code=5" not in text:
        return f"журнал не містить start/exit для pid={pid}: {text!r}"


@test
def t_stdio_once():
    out = os.path.join(TMP, "stdio.txt")
    with open(out, "w") as f:
        subprocess.run([LAUNCH, "true"], stdout=f, stderr=subprocess.DEVNULL, close_fds=True, timeout=10)
    if os.path.getsize(out):
        return "launch сам щось друкує у stdout (або дублює буфер stdio після fork)"


@test
def t_daemon():
    pidf = os.path.join(TMP, "d.pid")
    out = os.path.join(TMP, "d.out")
    for p in (pidf, out):
        if os.path.exists(p):
            os.unlink(p)
    t0 = time.monotonic()
    r = run([LAUNCH, "-d", "-p", pidf, "-o", out, "--", SELF, "-s", "2"], timeout=5)
    dt = time.monotonic() - t0
    if r.returncode != 0:
        return f"код {r.returncode}, stderr {r.stderr.strip()!r}"
    if dt > 1.5:
        return f"launch -d чекав на демона {dt:.1f} с — має повертатися одразу"
    for _ in range(50):
        if os.path.exists(out) and os.path.getsize(out):
            break
        time.sleep(0.05)
    with open(out) as f:
        i = report(f.read())
    with open(pidf) as f:
        pid = f.read().strip()
    expect(pid == i.get("pid"), f"PID-файл {pid!r} ≠ PID демона {i.get('pid')!r}")
    expect(i.get("sid") == i.get("pgid") and i.get("sid") != i.get("pid"),
           f"очікувалось: нова сесія і демон — не її лідер (pid={i.get('pid')} sid={i.get('sid')})")
    expect(i.get("ctty") == "none", "у демона є керуючий термінал")
    expect(i.get("cwd") == "/", f"cwd демона {i.get('cwd')!r}, очікувався /")
    expect(i.get("umask") == "0022", f"umask демона {i.get('umask')}, очікувався 0022")
    fds = i.get("fd", {})
    expect(fds.get("0") == "/dev/null", f"stdin демона → {fds.get('0')!r}, очікувався /dev/null")
    expect(fds.get("2") == out, f"stderr демона → {fds.get('2')!r}, очікувався файл -o")
    expect(set(fds) == {"0", "1", "2"}, f"у демона зайві дескриптори: {sorted(fds)}")


@test
def t_daemon_fail():
    pidf = os.path.join(TMP, "d2.pid")
    r = run([LAUNCH, "-d", "-p", pidf, "no-such-program-lab05"], timeout=5)
    if r.returncode != 127:
        return f"код {r.returncode}, очікувався 127: батько має дізнатися, що exec у демона не вдався"
    if os.path.exists(pidf):
        return "PID-файл створено для демона, який не запустився"


CALC_IN = ("1+2\n2*(3+4)\n1/0\n5-\n10/3\n(1\n7\n"
           "9223372036854775807+1\n-9223372036854775807-1\n(-9223372036854775807-1)/-1\n"
           "99999999999999999999\n3037000500*3037000500\n((((1))))\n")
CALC_OUT = ("3\n14\nerror: division by zero\nerror: syntax error\n3\nerror: expected )\n7\n"
            "error: overflow\n-9223372036854775808\nerror: overflow\nerror: overflow\nerror: overflow\n1\n"
            "lines=13 errors=7\n")


@test
def t_calc():
    r = run([CALC], stdin=CALC_IN)
    if r.returncode < 0:
        return f"calc завершився сигналом {-r.returncode} — яким рядком вхідних даних?"
    got, want = r.stdout.splitlines(), CALC_OUT.splitlines()
    for n, (g, w) in enumerate(zip(got, want), 1):
        if g != w:
            return f"рядок {n}: {g!r}, очікувалось {w!r}"
    if len(got) != len(want):
        return f"рядків виводу {len(got)}, очікувалось {len(want)}"
    if r.returncode != 1:
        return f"код {r.returncode}, очікувався 1 (були помилки)"


@test
def t_calc_deep():
    r = run([CALC], stdin="(" * 300 + "1" + ")" * 300 + "\n2+2\n")
    if r.stdout != "error: too deep\n4\nlines=2 errors=1\n":
        return f"глибока вкладеність: {r.stdout[:80]!r}"


@test
def t_calc_leak():
    if not shutil.which("valgrind"):
        return "valgrind не встановлено"
    r = run(["valgrind", "-q", "--leak-check=full", "--error-exitcode=9", CALC],
            stdin=CALC_IN, timeout=60)
    if r.returncode == 9 or "definitely lost" in r.stderr:
        lost = [l for l in r.stderr.splitlines() if "lost" in l]
        return "Valgrind: " + (lost[0].split("== ", 1)[-1] if lost else "помилки пам'яті")


shutil.rmtree(TMP, ignore_errors=True)
print(f"\n{sum(results)}/{len(results)}")
sys.exit(0 if all(results) else 1)
