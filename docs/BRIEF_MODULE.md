# Contrat commun à toute story module (lu par chaque sous-agent)

1. Lire : EPICS.md, ARCHITECTURE.md, docs/SPEC.md, le header de ton module (FIGÉ) et ceux de ses dépendances.
2. Fichiers possédés : `src/<dom>/<mod>/*.c`, `src/<dom>/<mod>/CMakeLists.txt` (appel `p2p_module`),
   `tests/<dom>/<mod>/**`. Rien d'autre. Le `.h` public est en lecture seule.
3. Ordre : écrire d'abord les tests (`tests/<dom>/<mod>/test_*.c`, framework `tests/test.h`) couvrant chaque
   fonction du header, cas limites et erreurs ; les lancer (ils doivent échouer/ne pas compiler) ; puis implémenter.
   Noter le nombre de tests passant au 1er build complet (= « 1er jet ») et le nombre de cycles build→fix ensuite.
4. Gate : `scripts/check.sh <dom>/<mod>` code retour 0 (asan+ubsan, tsan si `.concurrent`, valgrind). Pas de
   `TODO`/`FIXME`. Fonctions ≤ 35 lignes, fichiers ≤ 500 lignes, lignes ≤ 120 colonnes. `CMAKE_BUILD_PARALLEL_LEVEL=2`.
5. Header insuffisant ou ambigu → NE PAS le modifier : ajouter une entrée dans ISSUES.md UNIQUEMENT par `echo "- I? [module] ..." >> ISSUES.md` (jamais réécrire le fichier : agents en parallèle ; numéro « I? », Opus renumérote),
   choisir l'interprétation la plus stricte et la documenter dans le .c, continuer. Bloquant → s'arrêter et le dire.
6. Hors scope (SPEC §2) → ne pas faire, signaler.
7. Rapport final ≤ 5 lignes : ligne 1 = ton id de modèle exact ; puis `check.sh` code retour, tests 1er jet (x/y),
   cycles de correction, bugs trouvés par sanitizers/valgrind après tests verts, entrées ISSUES.md ajoutées.
