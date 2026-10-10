"""N4 : 1000 messages echanges entre 2 instances. Mode natif : RSS avant/apres stabilisation (ecart < 1 Mo).
Mode --vg : meme scenario, instances sous valgrind memcheck dans le conteneur p2pmsg-dev (image docker/Dockerfile),
critere = 0 octet 'definitely lost' et 0 erreur memcheck. Usage : n4_leak.py [--vg] [N] [BIN]"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import p2p

ARGS = [a for a in sys.argv[1:] if a != "--vg"]
VG = "--vg" in sys.argv
COUNT = int(ARGS[0]) if ARGS else 1000
BIN = ARGS[1] if len(ARGS) > 1 else p2p.DEFAULT_BIN
IMAGE = "p2pmsg-dev"
VG_BUILD = os.path.join(p2p.ROOT, "build", "bench-vg")
VG_BIN = os.path.join(VG_BUILD, "src", "app", "cli", "p2pmsg")
UIDGID = f"{os.getuid()}:{os.getgid()}"


def build_vg_binary():
    inner = (f"cmake -S {p2p.ROOT} -B {VG_BUILD} -DCMAKE_BUILD_TYPE=Debug -DP2P_VALGRIND=OFF >/dev/null "
             f"&& cmake --build {VG_BUILD} -j2 --target p2pmsg")
    subprocess.run(["docker", "run", "--rm", "-u", UIDGID, "-e", "HOME=/tmp", "-v", f"{p2p.ROOT}:{p2p.ROOT}",
                    "-w", p2p.ROOT, IMAGE, "bash", "-c", inner], check=True)


def start_vg(_binary, wdir, name, port):
    d = os.path.join(wdir, name)
    os.makedirs(d, exist_ok=True)
    cmd = ["docker", "run", "--rm", "-i", "--network", "host", "-u", UIDGID, "-e", "HOME=/tmp",
           "-v", f"{p2p.ROOT}:{p2p.ROOT}", "-w", p2p.ROOT, IMAGE,
           "valgrind", "--leak-check=full", "--errors-for-leak-kinds=definite", "--error-exitcode=99",
           f"--log-file={d}/valgrind.log", VG_BIN, "--dir", d, "--name", name, "--port", str(port), "--no-mdns"]
    return p2p.Inst(cmd, d, name)


def vg_report(wdir):
    for name in ("A", "B"):
        with open(os.path.join(wdir, name, "valgrind.log")) as f:
            text = f.read()
        lost = re.findall(r"definitely lost: ([\d,]+) bytes", text)
        errs = re.findall(r"ERROR SUMMARY: (\d+) errors", text)
        clean = "no leaks are possible" in text
        print(f"VG {name} definitely_lost={lost[-1] if lost else ('0 (no leaks are possible)' if clean else 'ABSENT')}"
              f" errors={errs[-1] if errs else 'ABSENT'}")


def run_messages(a, key_b):
    a.send(f"/to {key_b}")
    cursor, fails = 0, 0
    for i in range(COUNT):
        rtt, cursor = p2p.send_and_wait(a, key_b, f"leak {i}", cursor, 30 if VG else 5)
        fails += rtt is None
    time.sleep(2)  # laisse le thread lecteur de B vider sa sortie avant le comptage RECV
    return fails


def run_native(wdir):
    a, b, _, key_b = p2p.connected_pair(BIN, wdir)
    time.sleep(5)
    rss_before = a.rss_kb()
    fails = run_messages(a, key_b)
    time.sleep(10)
    rss_after = a.rss_kb()
    print(f"N4 n={COUNT} fail={fails} recv_B={b.count('RECV ')} RSS_avant={rss_before} kB "
          f"RSS_apres={rss_after} kB ecart={(rss_after - rss_before) / 1024:.3f} Mo")
    return a, b


def run_vg(wdir):
    build_vg_binary()
    a, b, _, key_b = p2p.connected_pair(BIN, wdir, start_fn=start_vg)
    fails = run_messages(a, key_b)
    print(f"N4-VG n={COUNT} fail={fails} recv_B={b.count('RECV ')}")
    for inst in (a, b):
        inst.send("/quit")
        inst.proc.wait(180)
    return a, b


def main():
    wdir = tempfile.mkdtemp(prefix="n4-", dir=os.path.join(p2p.ROOT, "build"))
    a = b = None
    try:
        a, b = run_vg(wdir) if VG else run_native(wdir)
        if VG:
            vg_report(wdir)
    finally:
        for inst in (a, b):
            if inst is not None:
                inst.stop()
        shutil.rmtree(wdir, ignore_errors=True)


if __name__ == "__main__":
    main()
