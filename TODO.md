# TODO

## S0 (en cours)
- [ ] Valgrind bloqué sur cette machine : valgrind 3.25.1 ne démarre aucun binaire (redirection `memcmp` dans ld.so, glibc 2.44+r24 installé vs r50 au dépôt). Mettre à jour glibc/valgrind puis relancer `scripts/check.sh`.
- [ ] Vérifier le workflow CI sur GitHub (ubuntu-latest) : non exécuté localement.

## Backlog
- [ ] Choisir submodule git vs clone CI pour noise-c (actuellement clone CI + entrée .gitignore).
- [ ] F2 + N1 LAN sur deux vraies machines (tour + portable) : à faire avec Chris une fois la GUI complète et utilisable (portable en veille le 10/10).
