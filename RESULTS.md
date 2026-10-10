# Résultats d'acceptation (SPEC §9)

Mesures du 10/10/2026 sur la tour (12 cœurs, 48 Go), build `release` (CLI p2pmsg seul), `asan` pour ctest/e2e.
Scripts : `scripts/bench/` (`p2p.py` = helpers communs, instances pilotées par pipes, `/proc` pour RSS/CPU).

| critère | statut | valeur | commande |
|---|---|---|---|
| N1 RTT médiane envoi→delivered < 5 ms | PASS | 0,213 ms (n=1000, 1000 ok) après envoi immédiat + TCP_NODELAY | `python3 scripts/bench/n1_rtt.py 1000` |
| N1 p99 < 20 ms | PASS | 2,223 ms (max 3446 ms : une valeur aberrante isolée, non expliquée) | idem |
| N2 VmRSS CLI au repos, 1 pair, t=30 s < 30 Mo | PASS | A 5,53 Mo, B 5,41 Mo | `python3 scripts/bench/n2_n3_idle.py` |
| N3 CPU au repos < 0,5 % (fenêtre 60 s après 30 s) | PASS | A 0,017 %, B 0,033 % | idem |
| N4 écart RSS après 1000 messages < 1 Mo | PASS | 0,055 Mo (5504 → 5560 kB) | `python3 scripts/bench/n4_leak.py 1000` |
| N4 valgrind (conteneur p2pmsg-dev), pas de fuite définie | PASS | A et B : 0 octet definitely lost, 0 erreur, 1000 RECV | `python3 scripts/bench/n4_leak.py --vg 1000` |
| N5 `scripts/check.sh` complet | PASS | code 0, RESULT: PASS (asan build+ctest, tsan, valgrind) | `scripts/check.sh` |
| Q1 build release -Werror | PASS | build release complet exit 0 | `cmake --build build/release -j2` |
| F1 deux instances, message + ACK (statique, mDNS) | PASS | 118 ms (statique) ; mDNS PASS dans p2pmsg-dev (avahi hôte inactif) | `scripts/e2e.sh` |
| F2 deux machines LAN | NON MESURÉ | portable en veille, non sollicité (demande Chris) : à faire avec lui une fois la GUI terminée | — |
| F3 WireGuard netns un sous-réseau WireGuard dédié | NON MESURÉ | `sudo -n` exige un mot de passe ; `wg` absent | `sudo -n true` ; `command -v wg` |
| F4 clé inattendue rejetée | PASS | pas de CONNECTED ni RECV avec mauvaise clé | `scripts/e2e.sh` (cas F4) |
| F5 message pendant coupure livré sans doublon | PASS | 11/11 peer/manager (dont test_pm_outage, test_pm_dedup) | `ctest --test-dir build/asan -L peer/manager` |
| F6 kill -9, pending repris | PASS | 2 pending repris, 2 RECV | `scripts/e2e.sh` (cas F6) |
| F7 fuzz parser trames ≥ 10 min sans crash | PASS (preuve existante, non relancée) | 46,7 M exécutions / 601 s, 0 crash | `METRICS.md` ligne J3 S5 |
| F8 vecteurs Noise XX | PASS | sec/session 5/5 dont test_vectors | `ctest --test-dir build/asan -L sec/session` |

## Notes
- N1 : corrigé (était 50 ms, envoi différé au tick de 50 ms). Valeur aberrante de 3,4 s à investiguer (ISSUES.md).
- N1 horodatage côté script (`time.monotonic_ns`), pas en C dans app/api : choix de simplicité ; même horloge monotone que clock_gettime(CLOCK_MONOTONIC).
- N2/N3 : CPU sur fenêtre [30 s, 90 s] ; RSS relevé à 30 s.
- N4 valgrind : un premier passage a donné 999 RECV côté B (course de comptage : le lecteur de sortie n'avait pas fini) ; le script attend désormais 2 s avant comptage, relancé : 1000 RECV.
- Q1 : corrigé.
- `scripts/e2e.sh` contient des `pkill -f` limités à son dossier temporaire unique ; un processus étranger (PID 1550329, `build/asan/.../p2pmsg --port 47901`, lancé hors de moi) reste actif et n'a pas été touché.
- Les scripts de bench ne tuent que leurs PID (`Popen`) ; ports libres choisis à l'exécution.
