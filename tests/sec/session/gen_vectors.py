"""Genere vectors_xx.h depuis les vecteurs Noise XX de noise-c (third_party, lecture seule).

Usage : python3 tests/sec/session/gen_vectors.py
Toutes les entrees "Noise_XX_25519_ChaChaPoly_BLAKE2b" de cacophony.txt et noise-c-basic.txt.
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
VEC_DIR = os.path.join(ROOT, "third_party", "noise-c", "tests", "vector")
OUT = os.path.join(ROOT, "tests", "sec", "session", "vectors_xx.h")
SOURCES = ["cacophony.txt", "noise-c-basic.txt"]
NAME = "Noise_XX_25519_ChaChaPoly_BLAKE2b"


def extract(text):
    """Renvoie tous les objets JSON dont le champ name vaut NAME."""
    decoder = json.JSONDecoder()
    found = []
    marker = '"name": "%s"' % NAME
    pos = text.find(marker)
    while pos != -1:
        start = text.rfind("{", 0, pos)
        obj, _ = decoder.raw_decode(text, start)
        found.append(obj)
        pos = text.find(marker, pos + len(marker))
    return found


def c_bytes(hex_str):
    raw = bytes.fromhex(hex_str)
    return "{" + ", ".join("0x%02x" % b for b in raw) + "}", len(raw)


class Emitter:
    def __init__(self):
        self.lines = []

    def array(self, name, hex_str):
        body, size = c_bytes(hex_str)
        self.lines.append("static const uint8_t %s[%d] = %s;" % (name, size, body))
        return size


def emit_vector(em, idx, source, vec):
    prefix = "v%d" % idx
    fields = {}
    for key in ("init_prologue", "init_static", "init_ephemeral",
                "resp_prologue", "resp_static", "resp_ephemeral"):
        fields[key] = em.array("%s_%s" % (prefix, key), vec[key])
    hash_len = 0
    if "handshake_hash" in vec:
        hash_len = em.array("%s_hash" % prefix, vec["handshake_hash"])
    msg_entries = []
    for i, msg in enumerate(vec["messages"]):
        pl = em.array("%s_m%d_payload" % (prefix, i), msg["payload"])
        ct = em.array("%s_m%d_ciphertext" % (prefix, i), msg["ciphertext"])
        msg_entries.append("    {%s_m%d_payload, %d, %s_m%d_ciphertext, %d}" % (prefix, i, pl, prefix, i, ct))
    em.lines.append("static const xx_vector_msg %s_msgs[] = {" % prefix)
    joined = [e + "," for e in msg_entries[:-1]] + ([msg_entries[-1] + "};"] if msg_entries else ["};"])
    em.lines.extend(joined)
    hash_ptr = "%s_hash" % prefix if hash_len else "NULL"
    em.lines.append("static const xx_vector %s = {" % prefix)
    em.lines.append('    "%s", %s_init_prologue, %d, %s_init_static, %s_init_ephemeral,'
                    % (source, prefix, fields["init_prologue"], prefix, prefix))
    em.lines.append("    %s_resp_prologue, %d, %s_resp_static, %s_resp_ephemeral,"
                    % (prefix, fields["resp_prologue"], prefix, prefix))
    em.lines.append("    %s, %d, %d, %s_msgs" % (hash_ptr, hash_len, len(vec["messages"]), prefix))
    em.lines.append("};")
    return prefix


HEADER = """/* Genere par tests/sec/session/gen_vectors.py depuis third_party/noise-c/tests/vector. Ne pas editer. */
#ifndef P2P_TESTS_SEC_SESSION_VECTORS_XX_H
#define P2P_TESTS_SEC_SESSION_VECTORS_XX_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *payload;
    size_t         payload_len;
    const uint8_t *ciphertext;
    size_t         ciphertext_len;
} xx_vector_msg;

typedef struct {
    const char    *source;
    const uint8_t *init_prologue;
    size_t         init_prologue_len;
    const uint8_t *init_static;
    const uint8_t *init_ephemeral;
    const uint8_t *resp_prologue;
    size_t         resp_prologue_len;
    const uint8_t *resp_static;
    const uint8_t *resp_ephemeral;
    const uint8_t *handshake_hash; /* NULL si le vecteur n'en porte pas */
    size_t         handshake_hash_len;
    size_t         msg_count;
    const xx_vector_msg *msgs;
} xx_vector;
"""


def main():
    entries = []
    for source in SOURCES:
        with open(os.path.join(VEC_DIR, source), encoding="utf-8") as fh:
            text = fh.read()
        for vec in extract(text):
            entries.append((source, vec))
    if not entries:
        sys.exit("aucun vecteur %s" % NAME)
    em = Emitter()
    names = [emit_vector(em, i, src, vec) for i, (src, vec) in enumerate(entries)]
    out = [HEADER] + [l for l in em.lines] + ["", "#define XX_VECTOR_COUNT %d" % len(names), ""]
    out.append("static const xx_vector *const XX_VECTORS[XX_VECTOR_COUNT] = {%s};" % ", ".join("&" + n for n in names))
    out.append("")
    out.append("#endif")
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out) + "\n")
    print("%d vecteur(s) ecrit(s) dans %s" % (len(entries), OUT))


if __name__ == "__main__":
    main()
