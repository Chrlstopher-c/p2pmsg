# EPICS — p2pmsg

Spec source : docs/SPEC.md. Décisions validées par Chris (10/10/2026) : noise-c figé sur
cfe25410979a87391bb9ac8d4d4bef64e9f268c6, fichier `peers` en texte simple (`ip:port pubkey_hex`),
mDNS via Avahi (libavahi-client). Machines : deux machines Linux, WireGuard entre les deux.

| Jalon | Stories (parallèles dans un jalon) | Gate |
|---|---|---|
| J0 | S0 infra : CMake, presets asan/tsan/release, noise-c en sous-build, scripts check, CI | `scripts/check.sh` vert sur projet vide |
| J1 | S1 core/log · S2 core/buf | check.sh vert, valgrind propre |
| J2 | S3 core/loop · S4 net/tcp | idem + TSan |
| J3 | S5 proto/frame (+ fuzz) · S6 proto/msg | idem, fuzz 10 min |
| J4 | S7 sec/session + vecteurs Noise XX | F8 |
| J5 | S8 net/discovery | idem |
| J6 | S9 peer/manager | F4, F5 |
| J7 | S10 store/history | F6 (partiel) |
| J8 | S11 app/cli + test E2E 2 instances | F1, F6 |
| J9 | S12 ui/gtk | parcours manuels §9 |

Règle : un sous-agent touche uniquement `src/<module>/` et `tests/<module>/`. Changement d'interface → ISSUES.md, stop.
Done = `scripts/check.sh <module>` code retour 0 (build -Werror, tests asan+ubsan, tsan si concurrent, valgrind).
