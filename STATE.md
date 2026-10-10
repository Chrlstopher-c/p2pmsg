# STATE — p2pmsg

## S0 infra (2026-10-10)
- Fait : CMake racine + `cmake/` (p2p_module, pkg-config p2p::sqlite3/avahi/gtk4, noise-c en ExternalProject hors source), presets asan/tsan/release/debug, `tests/test.h`, `scripts/check.sh`, `scripts/grep-todo.sh`, start/stop/restart, CI.
- Vérifié : `check.sh` sur projet vide = 0 ; release configure+build = 0 ; `check.sh core/dummy` : asan OK (test noise_init lié), tsan OK, leak détecté par LSan (test fuyant = échec), grep-todo détecte un orphelin ; module factice supprimé.
- Bloquant : valgrind inopérant sur cette machine (voir TODO.md). L'étape valgrind de check.sh échoue donc ici.
- Non committé.

## S14 packaging Linux (2026-10-10)
- Fait : `cmake --install` (bin/ p2pmsg + p2pmsg-gtk, share/doc/p2pmsg README+LICENSE), LICENSE MIT, `scripts/package.sh <version>` (build cibles p2pmsg/p2pmsg-gtk, staging, tar.gz + SHA256SUMS dans dist/), `release.yml` (tag v*, ubuntu-24.04, check.sh CHECK_NO_DOCKER=1, package, gh release), README FR.
- Vérifié : package.sh 0.0.0-test produit l'archive, SHA256SUMS OK, ldd sans dépendance manquante, p2pmsg démarre (SELF + /quit rc 0), YAML release/ci valides (pyyaml).
- Non vérifié : release.yml et ci.yml pas encore exécutés sur GitHub (dépôt public à venir).
- A corriger hors périmètre : `start.sh` pointe sur build/release/src/app/p2pmsg (binaire réel : build/release/src/app/cli/p2pmsg).
