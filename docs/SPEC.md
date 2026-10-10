# Spec : messagerie peer-to-peer en réseau local (C)

Copie de la spec fournie par Chris le 10/10/2026. Écarts validés : voir ISSUES.md (E1, E2).

## 2. Périmètre
v1 : découverte mDNS + liste statique ; TCP direct ; Noise XX ; messages texte avec ACK ; historique SQLite ;
GUI GTK4 minimale ; fonctionne via WireGuard.
Hors scope : NAT traversal, serveur/relais/comptes, fichiers/audio/vidéo, groupes, chiffrement au repos, multi-appareils.
Tout débordement est signalé, pas réalisé.

## 3. Contraintes
C11, Linux. Pas de crypto maison. Pas de dépendance non listée. ASan+UBSan partout, TSan sur modules concurrents,
builds test/release séparés. Valgrind memcheck propre avant merge. `-Wall -Wextra -Werror`.

## 4.1 Modules
| Module | Responsabilité | Dépend de |
|---|---|---|
| core/log | logging structuré, niveaux, stderr + fichier | — |
| core/buf | buffers d'octets, lecture/écriture bornées, parsing incrémental | — |
| core/loop | event loop epoll non bloquant, timers, dispatch | core/log |
| net/tcp | sockets TCP non bloquants, connexion, écoute, fermeture propre | core/loop, core/buf |
| net/discovery | mDNS + liste statique de pairs | core/loop, core/log |
| proto/frame | trames longueur préfixée, taille max | core/buf |
| proto/msg | HELLO, MSG, ACK, PING, PONG, BYE, sérialisation | proto/frame |
| sec/session | handshake Noise, chiffrement des trames, rekeying | net/tcp, proto/frame, noise-c |
| peer/manager | état des pairs, reconnexion, file d'envoi | sec/session, net/discovery |
| store/history | persistance SQLite, requêtes par conversation | core/log, sqlite3 |
| app/cli | client CLI (tests d'intégration) | peer/manager, store/history |
| ui/gtk | GUI GTK4 | app (API publique uniquement) |

## 4.2 Découverte
LAN : mDNS service `_p2pmsg._tcp`, TXT avec nom d'affichage et clé publique.
WireGuard : liste statique (ip:port + clé publique attendue), voie principale en VPN.
Clé publique vérifiée au handshake ; clé différente de l'attendue → rejet.

## 4.3 Trames
En-tête : longueur 4 octets big-endian, type 1 octet, puis payload. Max 64 Kio, au-delà fermeture.
Trame non conforme (longueur incohérente, type inconnu, tronqué) → fermeture, pas de récupération.
Parsing incrémental : une trame en plusieurs lectures, plusieurs trames dans une lecture.

## 4.4 Messages
HELLO (nom, version proto, après handshake) · MSG (id 8 octets, horodatage, texte UTF-8 max 4 Kio) ·
ACK (id) · PING/PONG (vide, toutes les 15 s) · BYE (raison optionnelle).
Livré = ACK reçu. Sans ACK 10 s → renvoi, max 5 tentatives. Doublons ignorés côté récepteur via l'id.

## 5. Sécurité
Noise XX via noise-c (Curve25519, ChaCha20-Poly1305, BLAKE2b). Clé d'identité Curve25519 par installation,
générée au premier lancement, fichier 0600. Vecteurs de test Noise publics dans les tests de sec/session.
Trame hors handshake avant session établie → rejet + fermeture. Logs : jamais de contenu ni de clé privée.

## 6. Persistance
SQLite3 WAL. Tables `peers(id, name, pubkey, last_seen)`, `messages(id, peer_id, direction, ts, body, status)`.
Écriture `pending` avant envoi, `delivered` à l'ACK. Au redémarrage les `pending` sont repris.

## 7. GUI
GTK4 sombre. Pairs à gauche (nom, statut), conversation à droite (bulles, heure, état d'envoi). Entrée envoie,
Maj+Entrée saut de ligne. Palette : fond #0c0d10 → #232731, vert #00d88a, rouge #ff4d6a, bleu #4a9eff, amber #f0a030.

## 9. Critères
F1 deux instances même machine via mDNS, message + ACK. F2 deux machines LAN. F3 WireGuard via liste statique.
F4 clé inattendue rejetée (auto). F5 message pendant coupure livré après reconnexion sans doublon (auto).
F6 kill -9 → pending repris. F7 fuzz parser trames ≥10 min sans crash. F8 vecteurs Noise XX passent.
N1 RTT médiane <5 ms, p99 <20 ms. N2 RSS repos <30 Mo CLI, <80 Mo GUI. N3 CPU repos <0,5 % sur 60 s.
N4 pas de fuite après 1000 messages. N5 zéro erreur TSan/ASan/UBSan.
Q1 -Werror partout. Q2 tests par module. Q3 aucun TODO/FIXME sans entrée ISSUES.md.

## 12. Livrables
src/, tests/, CMakeLists.txt, README.md, ISSUES.md, METRICS.md, RESULTS.md.
