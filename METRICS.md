# Métriques (§10)

Début J0 : 2026-10-10 (heure en tête de chaque ligne).

| Jalon | Story | Agent | Tokens (total) | Cycles correction | Tests OK 1er jet | Bugs sanitizers post-tests | Modifs interface |
|---|---|---|---|---|---|---|---|
04:55
| J0 | S0 infra | claude-haiku-5-5 | 115 784 | 1 (cas tsan sans test) | n/a | n/a | 0 |
| J1 | S2 core/buf | claude-haiku-5-5 | 108 243 | 0 | 16/16 | 0 | 0 |
| J1 | S1 core/log | claude-haiku-5-5 | 126 259 | 2 (+1 arbitrage I2) | 19/19 | 0 | 0 (3 ambiguïtés I1-I3) |
| J2 | S3 core/loop | claude-haiku-5-5 | 113 102 | 1 (split taille fichier) | 4/4 binaires (pas de rouge préalable mesuré) | 0 | 0 (I4-I6) |
| J2 | S4 net/tcp | claude-haiku-5-5 | 124 929 | 2 | 9/11 | 0 | 0 (I4-I9) |
| J3 | S6 proto/msg | claude-haiku-5-5 | 120 148 | 1 | 17/19 | 0 | 0 |
| J7 | S10 store/history | claude-haiku-5-5 | 114 881 | 2 (+1 build) | 0/17 (build KO) puis 17/17 | 0 | 0 |
| J5 | S8 net/discovery | claude-haiku-5-5 | 163 808 | ? | 25+/25 statique, mDNS non exécuté | 0 | 0 |
| J3 | S5 proto/frame | claude-haiku-5-5 | 99 552 | 2 | 11/12 | 0 | 0 — fuzz F7 : 46,7 M exécutions / 601 s, 0 crash |
| J4 | S7 sec/session | claude-haiku-5-5 | 134 888 | ~6 | 2/5 binaires (build KO) | 1 (ASan heap overflow sn_decrypt) | 0 — F8 vecteurs XX OK |
| J6 | S9 peer/manager | claude-haiku-5-5 | 104 523 | 4 (harnais de test) | n/a (tests écrits après) | 0 restant (fuites harnais corrigées) | 0 — F4, F5 automatisés verts |
| J8 | S11 app/api+cli | claude-haiku-5-5 | 116 618 | 3 | cli 1er jet OK, api test_resume KO (setup) | 1 fuite pm_free (renvoyée à S9) | 0 — E2E F1-static/F1-mdns/F4/F6 PASS |
| J9 | S12 ui/gtk (v1+v2) | claude-haiku-5-5 | 287 598 | 3 bugs UI (sélection auto, placeholder, Entrée) | 15/15 modèle | 0 | 0 — RSS GUI 48,6 Mo |
| J9 | S12b (doublon lancé par erreur Opus) | claude-haiku-5-5 | 136 144 | 0 | n/a | n/a | 0 — coût de coordination |
| Post-J9 | S13 mesures | claude-haiku-5-5 | 124 398 | 0 | n/a | 2 défauts trouvés (N1 50 ms, Q1 release) | 0 |
| Post-J9 | S14 release | claude-haiku-5-5 | 102 932 | 0 | n/a | n/a | 0 |
| Post-J9 | CI GitHub | — | — | 2 (test_io -Werror Ubuntu, test wire instable) | — | — | 0 |

Fin : release v0.1.0 publiée le 2026-10-10 vers 07:00 (début J0 vers 04:00). F3 non validé (WireGuard réel requis) → horloge « J1 → F3 » non close.
Tokens Opus orchestrateur : non mesurables depuis la session (à relever dans la console).
