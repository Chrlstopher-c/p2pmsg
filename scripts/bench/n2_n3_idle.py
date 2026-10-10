"""N2 : VmRSS du CLI a t=30 s (1 pair connecte). N3 : CPU au repos sur [30 s, 90 s] (utime+stime, /proc/<pid>/stat).
Seuils : RSS < 30 Mo, CPU < 0,5 %. Usage : n2_n3_idle.py [BIN]"""
import os
import shutil
import sys
import tempfile
import time

import p2p

BIN = sys.argv[1] if len(sys.argv) > 1 else p2p.DEFAULT_BIN
WARMUP_S = 30
WINDOW_S = 60


def main():
    wdir = tempfile.mkdtemp(prefix="n2-", dir=os.path.join(p2p.ROOT, "build"))
    a = b = None
    try:
        a, b, _, _ = p2p.connected_pair(BIN, wdir)
        time.sleep(WARMUP_S)
        rss_a, rss_b = a.rss_kb(), b.rss_kb()
        ta0, tb0 = a.cpu_ticks(), b.cpu_ticks()
        time.sleep(WINDOW_S)
        cpu_a = (a.cpu_ticks() - ta0) / p2p.CLK_TCK / WINDOW_S * 100
        cpu_b = (b.cpu_ticks() - tb0) / p2p.CLK_TCK / WINDOW_S * 100
        print(f"N2 RSS A={rss_a / 1024:.2f} Mo  B={rss_b / 1024:.2f} Mo (t={WARMUP_S} s)")
        print(f"N3 CPU A={cpu_a:.3f} %  B={cpu_b:.3f} % (fenetre {WINDOW_S} s apres {WARMUP_S} s)")
    finally:
        for inst in (a, b):
            if inst is not None:
                inst.stop()
        shutil.rmtree(wdir, ignore_errors=True)


if __name__ == "__main__":
    main()
