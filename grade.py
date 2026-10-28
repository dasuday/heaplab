#!/usr/bin/env python3
"""heaplab autograder.

usage: python3 grade.py [week1|week2|week3|all] [--traces DIR ...] [--json FILE] [--no-build]

Builds the project with `make`, runs the automatic tests for the chosen week(s) and prints a
score table. The same script is run by GitHub Actions on every push and by the instructor on
your tagged submissions (with additional hidden traces). Items marked "manual" are graded by
reading your code and are not included in the automatic total.
"""
import argparse
import glob
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(ROOT, "build")
LIBMM = os.path.join(BUILD, "libmm.so")
LIBMM_DEBUG = os.path.join(BUILD, "libmm-debug.so")

# performance index -> week 2 points: linear between these two values
PERF_ZERO, PERF_FULL = 0.60, 0.88
UTIL_ZERO, UTIL_FULL = 0.40, 0.70
# The starter "bump" allocator is correct but never reuses memory. Items that a non-reusing
# allocator would pass for free only count once average utilization reaches this level.
REUSE_MIN = 0.50

_util_cache = {}


def utilization(dirs):
    """Average utilization over the first trace directory (cached)."""
    if "u" not in _util_cache:
        code, out, err = run([os.path.join(BUILD, "mdriver"), "-n", "-j", "-t", dirs[0]], timeout=600)
        u = 0.0
        for line in out.splitlines():
            if line.startswith("{"):
                u = json.loads(line)["avg_util"]
        _util_cache["u"] = u
    return _util_cache["u"]


def reuses(dirs):
    return utilization(dirs) >= REUSE_MIN

NEEDS_REUSE = f"(counts once utilization >= {REUSE_MIN:.0%}: the starter never reuses memory)"


def run(cmd, timeout=120, env=None, stdin=None, shell=False):
    """Run a command; returns (exit code or None on timeout, stdout, stderr)."""
    try:
        p = subprocess.run(cmd, cwd=ROOT, env=env, input=stdin, capture_output=True, timeout=timeout,
                           shell=shell)
        return p.returncode, p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace")
    except subprocess.TimeoutExpired:
        return None, "", "timed out after %ds" % timeout


def preload_env(lib):
    env = dict(os.environ)
    env["LD_PRELOAD"] = lib
    return env


class Report:
    def __init__(self):
        self.items = []

    def add(self, week, name, got, out_of, note="", manual=False):
        self.items.append({"week": week, "item": name, "points": round(got, 2), "max": out_of,
                           "note": note, "manual": manual})

    def show(self):
        week = None
        for it in self.items:
            if it["week"] != week:
                week = it["week"]
                print(f"\n{week}")
                print("-" * 78)
            pts = "manual" if it["manual"] else f"{it['points']:5.1f} / {it['max']:<4}"
            print(f"  {it['item']:<38} {pts:>13}  {it['note']}")
        auto = [i for i in self.items if not i["manual"]]
        got, out_of = sum(i["points"] for i in auto), sum(i["max"] for i in auto)
        manual = sum(i["max"] for i in self.items if i["manual"])
        print("\n" + "=" * 78)
        print(f"  automatic total: {got:.1f} / {out_of}" + (f"   (+ {manual} points graded by hand)" if manual else ""))
        return got, out_of


def count_pass(output, prefix=""):
    lines = [l for l in output.splitlines() if l.startswith(("PASS", "FAIL")) and prefix in l]
    return sum(l.startswith("PASS") for l in lines), lines


# ------------------------------------------------------------------------------------ week 1
def trace_files(dirs):
    files = []
    for d in dirs:
        files += sorted(glob.glob(os.path.join(d, "*.rep")))
    return files


def correctness(traces):
    """Run each trace on its own so that one crash costs only that trace."""
    ok, failed = 0, []
    for f in traces:
        code, out, err = run([os.path.join(BUILD, "mdriver"), "-n", "-v", "-f", f], timeout=300)
        if code == 0:
            ok += 1
        else:
            reason = "timed out" if code is None else ("crashed" if code < 0 else "incorrect")
            failed.append(f"{os.path.basename(f)} ({reason})")
    return ok, failed


def perf_run(dirs, reps=5):
    args = [os.path.join(BUILD, "mdriver"), "-j", "-l", "-r", str(reps)]
    tmp = None
    if len(dirs) > 1:                       # mdriver takes one directory: link all traces into one
        tmp = tempfile.mkdtemp(prefix="heaplab-traces-")
        for f in trace_files(dirs):
            os.symlink(os.path.abspath(f), os.path.join(tmp, os.path.basename(f)))
        args += ["-t", tmp]
    else:
        args += ["-t", dirs[0]]
    code, out, err = run(args, timeout=900)
    if tmp:
        shutil.rmtree(tmp)
    for line in reversed(out.splitlines()):
        if line.startswith("{"):
            return json.loads(line)
    return None


def week1(rep, dirs):
    W = "Week 1: address space and an implicit free list"
    code, out, err = run([os.path.join(BUILD, "tests", "vmmap_test")], timeout=60)
    passed, lines = count_pass(out)
    total = max(len(lines), 11)
    if passed <= 1:                          # "NULL is not mapped" alone passes for the empty starter
        passed = 0
    rep.add(W, "vm_find / vm_kind (vmmap_test)", 5 * passed / total, 5, f"{passed}/{total} checks")

    traces = trace_files(dirs)
    ok, failed = correctness(traces)
    gate = reuses(dirs)
    rep.add(W, "correct on every trace", 12 * ok / len(traces) if gate else 0, 12,
            f"{ok}/{len(traces)} traces" + (": " + ", ".join(failed[:4]) if failed else "")
            + ("" if gate else " " + NEEDS_REUSE))

    util = utilization(dirs)
    frac = min(1, max(0, (util - UTIL_ZERO) / (UTIL_FULL - UTIL_ZERO)))
    rep.add(W, f"utilization >= {UTIL_FULL:.0%}", 3 * frac, 3, f"average {util:.1%}")

    code, out, err = run([os.path.join(BUILD, "mdriver"), "-n", "-c", "-t", dirs[0]], timeout=900)
    note = "mdriver -c passes" if code == 0 else "mdriver -c reports errors (or a trace fails)"
    rep.add(W, "heap checker (mm_checkheap)", 0, 5, note, manual=True)


# ------------------------------------------------------------------------------------ week 2
def week2(rep, dirs):
    W = "Week 2: segregated free lists and performance"
    traces = trace_files(dirs)
    ok, failed = correctness(traces)
    gate = reuses(dirs)
    rep.add(W, "still correct on every trace", 5 * ok / len(traces) if gate else 0, 5,
            f"{ok}/{len(traces)} traces" + ("" if gate else " " + NEEDS_REUSE))
    res = perf_run(dirs)
    if res is None:
        rep.add(W, "performance index", 0, 25, "mdriver crashed or timed out")
    else:
        p = max(0.0, res["perf_index"])
        frac = min(1, max(0, (p - PERF_ZERO) / (PERF_FULL - PERF_ZERO)))
        note = (f"P = {100 * p:.1f} (util {100 * res['avg_util']:.1f}%, throughput "
                f"{res['throughput'] / 1e3:.0f} Kops/s = {100 * res['throughput'] / max(res['libc_throughput'], 1):.0f}% of glibc)")
        rep.add(W, f"performance index ({100 * PERF_ZERO:.0f} -> 0, {100 * PERF_FULL:.0f} -> full)", 25 * frac, 25, note)
    rep.add(W, "design comment and code quality", 0, 5, "read by the grader", manual=True)


# ------------------------------------------------------------------------------------ week 3
PY_SCRIPT = r"""
import json, threading, collections
d = {str(i): [i, str(i) * 3, {"k": i % 7}] for i in range(60000)}
s = json.dumps(d, sort_keys=True)
back = json.loads(s)
c = collections.Counter(len(v[1]) for v in back.values())
out = []
def work(n):
    out.append(sum(len(str(x)) for x in range(n)))
ts = [threading.Thread(target=work, args=(20000 + k,)) for k in range(8)]
[t.start() for t in ts]; [t.join() for t in ts]
print(len(s), sorted(c.items()), sorted(out))
"""


def real_programs(tmpdir):
    rng = random.Random(7)
    nums = os.path.join(tmpdir, "numbers.txt")
    with open(nums, "w") as f:
        f.write("\n".join(str(rng.randint(-10 ** 9, 10 ** 9)) for _ in range(200000)) + "\n")
    blob = os.path.join(tmpdir, "blob.txt")
    with open(blob, "w") as f:
        for i in range(60000):
            f.write(f"line {i} {rng.random()} the quick brown fox {i * 31 % 977}\n")
    return [
        ("ls -la /usr/bin", ["ls", "-la", "/usr/bin"], "ls"),
        ("python3: dicts, JSON, threads", ["python3", "-c", PY_SCRIPT], "python3"),
        ("sort -n of 200,000 numbers", ["sort", "-n", nums], "sort"),
        ("gzip | gunzip round trip", f"gzip -c {blob} | gunzip -c | md5sum", "gzip"),
        ("bash with 300 subshells", "for i in $(seq 1 300); do x=$(echo $i); done; echo $x", "bash"),
    ]


def week3(rep, dirs):
    W = "Week 3A: a real malloc"
    gate = reuses(dirs)
    code, out, err = run([os.path.join(BUILD, "tests", "api_test"), "3a"], timeout=120)
    passed, lines = count_pass(out, "3A")
    total = max(len(lines), 6)
    if not any(l.startswith("PASS") and "memalign alignments" in l for l in lines):
        passed -= any(l.startswith("PASS") and "rejects bad" in l for l in lines)   # NULL for everything
    rep.add(W, "memalign, usable size, calloc, mapping", 6 * passed / total if gate else 0, 6,
            f"{passed}/{total} checks" + ("" if code is not None and code >= 0 else " (crashed)")
            + ("" if gate else " " + NEEDS_REUSE))
    code, out, err = run([os.path.join(BUILD, "tests", "threads")], timeout=120, env=preload_env(LIBMM))
    threads_ok = code == 0 and gate
    rep.add(W, "8 threads at once", 3 if threads_ok else 0, 3,
            "ok" if threads_ok else ("timed out" if code is None else "failed or crashed" if code else NEEDS_REUSE))
    code, out, err = run([os.path.join(BUILD, "tests", "forks")], timeout=60, env=preload_env(LIBMM))
    forks_ok = code == 0 and threads_ok
    rep.add(W, "fork while threads allocate", 2 if forks_ok else 0, 2,
            "ok" if forks_ok else ("deadlock (timed out)" if code is None else "failed or crashed" if code
                                   else "counts once the threads test passes"))
    with tempfile.TemporaryDirectory(prefix="heaplab-") as tmp:
        progs = real_programs(tmp)
        good, tried, bad = 0, 0, []
        for name, cmd, exe in progs:
            if shutil.which(exe) is None:
                continue
            tried += 1
            shell = isinstance(cmd, str)
            if shell:
                cmd = ["bash", "-c", cmd]
            c0, o0, _ = run(cmd, timeout=120)
            c1, o1, e1 = run(cmd, timeout=120, env=preload_env(LIBMM))
            if c1 == c0 and o1 == o0:
                good += 1
            else:
                bad.append(name.split(":")[0])
        pts = 5 * good / tried if tried and gate else 0
        rep.add(W, "real programs under LD_PRELOAD", pts, 5,
                f"{good}/{tried} match glibc" + (": failed " + ", ".join(bad) if bad else "")
                + ("" if gate else " " + NEEDS_REUSE))

    W = "Week 3B: a debugging allocator"
    cases = [
        ("overflow", "heap-buffer-overflow", True),
        ("underflow", "heap-buffer-overflow", True),
        ("double_free", "double-free", True),
        ("invalid_free", "invalid-free", True),
        ("stack_free", "invalid-free", True),
        ("use_after_free", "use-after-free", True),
        ("leak", "leak: 4242 bytes", False),
    ]
    got, notes = 0, []
    for prog, want, must_fail in cases:
        code, out, err = run([os.path.join(BUILD, "tests", "debug", prog)], timeout=60, env=preload_env(LIBMM_DEBUG))
        ok = ("heaplab: " + want) in err and (code != 0 if must_fail else True) and code is not None
        got += ok
        if not ok:
            notes.append(prog)
    code, out, err = run([os.path.join(BUILD, "tests", "debug", "clean")], timeout=60, env=preload_env(LIBMM_DEBUG))
    clean_ok = code == 0 and "heaplab:" not in err and got > 0     # only counts once something is detected
    got += clean_ok
    if not clean_ok:
        notes.append("clean")
    rep.add(W, "8 programs under libmm-debug.so", 12 * got / 8, 12,
            f"{got}/8 diagnosed correctly" + (": missed " + ", ".join(notes) if notes else ""))

    W = "Week 3C: a garbage collector"
    code, out, err = run([os.path.join(BUILD, "tests", "api_test"), "3c"], timeout=60)
    passed, lines = count_pass(out, "3C")
    rep.add(W, "heap walk and gc bits", 2 * passed / max(len(lines), 1), 2, f"{passed}/{max(len(lines), 1)} checks")
    code, out, err = run([os.path.join(BUILD, "tests", "gc_test")], timeout=120)
    passed, lines = count_pass(out)
    total = 8
    note = f"{passed}/{total} checks"
    if code is None:
        note += " (timed out)"
    elif code < 0:
        note += " (crashed)"
    rep.add(W, "gc_test", 10 * passed / total, 10, note)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("week", nargs="?", default="all", choices=["week1", "week2", "week3", "all"])
    ap.add_argument("--traces", action="append", help="trace directory (repeatable; default: traces)")
    ap.add_argument("--json", help="also write the results to this file")
    ap.add_argument("--no-build", action="store_true", help="do not run make first")
    args = ap.parse_args()
    dirs = args.traces or [os.path.join(ROOT, "traces")]

    if not args.no_build:
        # rebuild everything with the standard flags, whatever the last build used
        code, out, err = run(["make", "-B", "-j2", "all", "CFLAGS=-O2 -g"], timeout=600)
        if code != 0:
            print(out[-3000:] + err[-3000:])
            print("\nmake failed: fix the build first (nothing was graded).")
            sys.exit(1)

    rep = Report()
    if args.week in ("week1", "all"):
        week1(rep, dirs)
    if args.week in ("week2", "all"):
        week2(rep, dirs)
    if args.week in ("week3", "all"):
        week3(rep, dirs)
    got, out_of = rep.show()
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"items": rep.items, "automatic": got, "automatic_max": out_of}, f, indent=1)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as f:
            f.write("| week | item | points | note |\n|---|---|---|---|\n")
            for it in rep.items:
                pts = "manual" if it["manual"] else f"{it['points']} / {it['max']}"
                f.write(f"| {it['week'].split(':')[0]} | {it['item']} | {pts} | {it['note']} |\n")
            f.write(f"\n**Automatic total: {got:.1f} / {out_of}**\n")


if __name__ == "__main__":
    main()
