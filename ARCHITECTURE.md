# Architecture

- `src/core/` : briques sans dépendance réseau (log, buffers, event loop). Utilisables par tous.
- `src/net/` : I/O réseau brut (TCP, découverte). Ne connaît ni le protocole ni la crypto.
- `src/proto/` : format des trames et des messages, pur (pas d'I/O).
- `src/sec/` : session Noise, seul module à toucher noise-c.
- `src/peer/` : état des pairs, reconnexion, file d'envoi, retransmissions.
- `src/store/` : persistance SQLite, seul module à toucher sqlite3.
- `src/app/` : API applicative publique (`app.h`) + binaire CLI. Seule porte d'entrée pour l'UI.
- `src/ui/` : GTK4, n'inclut que `app/app.h`.
- `tests/<module>/` : tests unitaires du module, un exécutable par fichier `test_*.c`.

Bornes : un module vit dans `src/<dom>/<mod>/`, header public `<mod>.h` inclus comme `"<dom>/<mod>/<mod>.h"` ; il n'inclut que ceux de ses dépendances déclarées (SPEC §4.1).
Préfixes : chaque symbole public est préfixé du module (`log_`, `buf_`, `loop_`, `tcp_`, `frame_`, `msg_`,
`sess_`, `disc_`, `pm_`, `hist_`, `app_`). Erreurs : `int` 0 = OK, <0 = `-errno` ou code module négatif.
