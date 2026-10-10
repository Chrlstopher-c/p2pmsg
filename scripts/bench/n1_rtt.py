"""N1 : RTT envoi -> STATUS delivered, 1000 messages séquentiels entre 2 instances release sur 127.0.0.1.
Horodatage côté script (time.monotonic_ns). Seuils : médiane < 5 ms, p99 < 20 ms. Usage : n1_rtt.py [N] [BIN]"""
import os
import shutil
import statistics
import sys
import tempfile

import p2p

COUNT = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
BIN = sys.argv[2] if len(sys.argv) > 2 else p2p.DEFAULT_BIN


def main():
    wdir = tempfile.mkdtemp(prefix="n1-", dir=os.path.join(p2p.ROOT, "build"))
    a = b = None
    try:
        a, b, _, key_b = p2p.connected_pair(BIN, wdir)
        a.send(f"/to {key_b}")
        cursor, rtts, fails = 0, [], 0
        for i in range(COUNT):
            rtt, cursor = p2p.send_and_wait(a, key_b, f"bench {i}", cursor, 5)
            if rtt is None:
                fails += 1
            else:
                rtts.append(rtt)
        print(f"n={COUNT} ok={len(rtts)} fail={fails} recv_B={b.count('RECV ')}")
        print(f"mediane={statistics.median(rtts):.3f} ms p99={p2p.percentile(rtts, 99):.3f} ms "
              f"max={max(rtts):.3f} ms moyenne={statistics.mean(rtts):.3f} ms")
    finally:
        for inst in (a, b):
            if inst is not None:
                inst.stop()
        shutil.rmtree(wdir, ignore_errors=True)


if __name__ == "__main__":
    main()
