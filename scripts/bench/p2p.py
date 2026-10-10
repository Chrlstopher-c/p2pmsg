"""Helpers de mesure : instances p2pmsg pilotées par pipes, lecture /proc. Ne tue que ses propres PID."""
import math
import os
import signal
import socket
import subprocess
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_BIN = os.path.join(ROOT, "build", "release", "src", "app", "cli", "p2pmsg")
CLK_TCK = os.sysconf("SC_CLK_TCK")


class Inst:
    """Une instance CLI : stdin écrit par le script, stdout lu par un thread, horodaté à l'arrivée."""

    def __init__(self, cmd, wdir, name):
        self.lines = []
        self.cond = threading.Condition()
        self.errf = open(os.path.join(wdir, name + ".err"), "w")
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.errf, text=True, bufsize=1)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            with self.cond:
                self.lines.append((time.monotonic_ns(), line.rstrip("\n")))
                self.cond.notify_all()

    def send(self, text):
        self.proc.stdin.write(text + "\n")
        self.proc.stdin.flush()

    def wait_line(self, pred, timeout, start=0):
        """Première ligne d'index >= start satisfaisant pred : (index, t_ns, ligne) ou None."""
        deadline = time.monotonic() + timeout
        seen = start
        with self.cond:
            while True:
                while seen < len(self.lines):
                    if pred(self.lines[seen][1]):
                        return (seen, self.lines[seen][0], self.lines[seen][1])
                    seen += 1
                left = deadline - time.monotonic()
                if left <= 0:
                    return None
                self.cond.wait(left)

    def count(self, prefix):
        with self.cond:
            return sum(1 for _, line in self.lines if line.startswith(prefix))

    def rss_kb(self):
        with open(f"/proc/{self.proc.pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
        return -1

    def cpu_ticks(self):
        with open(f"/proc/{self.proc.pid}/stat") as f:
            fields = f.read().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])  # utime + stime (champs 14 et 15)

    def stop(self, timeout=5):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.errf.close()


def native_cmd(binary, wdir, name, port):
    d = os.path.join(wdir, name)
    os.makedirs(d, exist_ok=True)
    return [binary, "--dir", d, "--name", name, "--port", str(port), "--no-mdns"]


def start_native(binary, wdir, name, port):
    return Inst(native_cmd(binary, wdir, name, port), os.path.join(wdir, name), name)


def capture_key(binary, wdir, name, port):
    inst = start_native(binary, wdir, name, port)
    try:
        hit = inst.wait_line(lambda s: s.startswith("SELF "), 10)
    finally:
        inst.stop()
    if hit is None:
        raise RuntimeError(f"{name}: pas de SELF")
    return hit[2].split()[1]


def write_peers(wdir, name, port, key, peer_name):
    with open(os.path.join(wdir, name, "peers"), "w") as f:
        f.write(f"127.0.0.1:{port} {key} {peer_name}\n")


def free_port():
    """Port TCP libre sur 127.0.0.1 (ports des processus étrangers jamais réutilisés)."""
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def connected_pair(binary, wdir, start_fn=start_native, key_fn=capture_key):
    """Deux instances (A, B) se connectant via liste statique ; renvoie (a, b, key_a, key_b)."""
    port_a, port_b = free_port(), free_port()
    key_a = key_fn(binary, wdir, "A", port_a)
    key_b = key_fn(binary, wdir, "B", port_b)
    write_peers(wdir, "A", port_b, key_b, "B")
    write_peers(wdir, "B", port_a, key_a, "A")
    a = start_fn(binary, wdir, "A", port_a)
    b = start_fn(binary, wdir, "B", port_b)
    if a.wait_line(lambda s: s.startswith(f"PEER {key_b} connected"), 20) is None:
        a.stop()
        b.stop()
        raise RuntimeError("A ne se connecte pas à B")
    return a, b, key_a, key_b


def send_and_wait(a, key_b, text, cursor, timeout):
    """Envoie text, attend SENT puis STATUS delivered. Renvoie (rtt_ms, nouveau_curseur) ou (None, cursor)."""
    t0 = time.monotonic_ns()
    a.send(text)
    sent = a.wait_line(lambda s: s.startswith(f"SENT {key_b} "), timeout, cursor)
    if sent is None:
        return None, cursor
    msg_id = sent[2].split()[2]
    done = a.wait_line(lambda s: s == f"STATUS {key_b} {msg_id} delivered", timeout, sent[0] + 1)
    if done is None:
        return None, sent[0] + 1
    return (done[1] - t0) / 1e6, done[0] + 1


def percentile(values, pct):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(pct / 100 * len(ordered)) - 1)]
