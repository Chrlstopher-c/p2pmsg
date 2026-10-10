# p2pmsg

Messagerie texte pair à pair sur réseau local (et via WireGuard), en C11, Linux x86_64.
Pas de serveur, pas de compte : chaque instance a une clé d'identité Curve25519, les sessions sont chiffrées avec Noise XX.

Spécification : `docs/SPEC.md` · jalons : `EPICS.md` · résultats d'acceptation : [RESULTS.md](RESULTS.md)

## Build

Dépendances Ubuntu 24.04 :
```
sudo apt install cmake build-essential autoconf automake libtool pkg-config \
  libsqlite3-dev libavahi-client-dev libgtk-4-dev valgrind bison flex
```

Dépendances Arch :
```
sudo pacman -S cmake gcc autoconf automake libtool pkgconf bison flex sqlite avahi gtk4 valgrind
```

noise-c est cloné dans `third_party/` à un commit figé (compilé automatiquement par CMake) :
```
git clone https://github.com/rweather/noise-c third_party/noise-c
git -C third_party/noise-c checkout cfe25410979a87391bb9ac8d4d4bef64e9f268c6
cmake --preset release && cmake --build build/release --target p2pmsg p2pmsg-gtk
```

Installation ou archive de release : `scripts/package.sh <version>` produit `dist/p2pmsg-<version>-linux-x86_64.tar.gz`.

Dépendances runtime des binaires : libgtk-4-1, libsqlite3-0, libavahi-client3.

## Lancer deux instances

CLI (`build/release/src/app/cli/p2pmsg`) :
```
build/release/src/app/cli/p2pmsg --dir ./data/alice --name alice --port 47001
build/release/src/app/cli/p2pmsg --dir ./data/bob --name bob   --port 47002
```
Au démarrage, l'instance affiche `SELF <clé_hex> <port>`. Commandes : `/peers`, `/to <clé>`, `/hist [clé]`, `/quit` ; toute autre ligne est envoyée au pair courant.

GUI (`build/release/src/ui/gtk/p2pmsg-gtk`) : mêmes options (`--dir`, `--name`, `--port`, `--no-mdns`, `--debug`). Pairs à gauche, conversation à droite ; Entrée envoie, Maj+Entrée saute une ligne. L'en-tête de la conversation locale affiche l'empreinte de sa clé ; le bouton « copier » place la clé publique complète dans le presse-papiers.

Options communes : `--no-mdns` désactive la découverte LAN (utile pour les tests avec liste statique seule).

## Fichier `peers` (liste statique)

Placé dans le dossier de données (`--dir`), une ligne par pair :
```
192.0.2.10:47001 3f9a...c1 alice
192.0.2.20:47002 8b12...e7 bob
```
Format : `ip:port clé_hex nom`. `clé_hex` est la clé publique complète (64 caractères hexadécimaux) ; une clé différente de celle attendue à la poignée de main fait rejeter la connexion.

Où trouver sa clé : la ligne `SELF` au démarrage de la CLI, ou le bouton copier de la GUI.

## WireGuard

En VPN, la découverte est statique : chaque côté liste l'autre dans son fichier `peers` avec l'adresse du tunnel (ex. `192.0.2.20:47002`) et la clé publique attendue.

## Tests

- `scripts/check.sh [<dom>/<mod>]` : build et tests asan+ubsan, tsan sur les modules concurrents, valgrind (conteneur Docker, ou natif avec `CHECK_NO_DOCKER=1`).
- `scripts/e2e.sh [F1-static F1-mdns F4 F6]` : deux instances sur 127.0.0.1.
- `scripts/fuzz.sh <module> <secondes>` : fuzzing libFuzzer (clang) du parseur, ex. `scripts/fuzz.sh frame 600`.
- `scripts/grep-todo.sh` : TODO/FIXME sans référence `ISSUES.md#`.

## État

Critères d'acceptation et mesures : [RESULTS.md](RESULTS.md).

## Limites

- Linux x86_64 uniquement ; Windows prévu en v2.
- Découverte LAN via Avahi : le démon avahi-daemon doit tourner.
- Pas de traversée NAT, pas de relais, pas de fichiers ni de groupes (hors périmètre v1).
