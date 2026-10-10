# Issues ouvertes / écarts à la spec

- E1 (décision) : `peers.toml` remplacé par `peers` texte simple (`ip:port pubkey_hex [nom]`) — pas de dépendance TOML.
- E2 (décision) : mDNS via Avahi (libavahi-client), dépendance ajoutée hors §5, validée par Chris le 10/10.
- E3 (env) : valgrind Arch hôte inutilisable (ld.so strippé) → étape valgrind exécutée dans Docker Ubuntu 24.04 (cible réelle).
- I1 [core/log] « Ligne tronquée à 1024 octets » : interprété comme 1024 octets `\n` final inclus (contenu ≤ 1023), troncature UTF-8 propre.
- I2 [core/log] `log_init(path)` avec chemin : sortie fichier seule (pas de stderr), conforme à « path NULL = stderr seul ». Fichier existant : `fchmod 0600` forcé en plus de l'`open` (sinon le mode ne vaut que pour une création). **RÉSOLU** : arbitrage coordinateur — stderr ET fichier quand path est donné (chaque ligne écrite sur les deux).
- I3 [core/log] `log_write` avant `log_init` ou après `log_shutdown` : stderr, niveau minimal conservé (INFO par défaut). Module NULL journalisé « ? ».
- E4 (process) : J2 démarré avant la gate valgrind de J1 (valgrind en migration Docker) ; asan/ubsan/tsan J1 verts. Valgrind J1 rejoué dès que S0b livre.
I4 [core/loop] loop_set_wake non précisé thread-safe : implémenté comme setter non synchronisé, à appeler avant de démarrer la boucle (seul loop_wake est multi-thread).
I5 [core/loop] loop_stop avant loop_run : loop_run remet stop à 0 au démarrage, donc le stop est perdu ; stop ne vaut que depuis un callback en cours.
I6 [core/loop] fd fermé par l'appelant sans loop_del_fd : l'entrée reste dans la liste jusqu'au del ; le contrat exige loop_del_fd avant close(). Non détecté.
- I4 [net/tcp] (RÉSOLU, header reformaté) `tcp.h` ligne 23 (commentaire on_close) dépasse 120 colonnes : header figé, non modifié.
- I5 [net/tcp] `tcp_send` après `tcp_close` ou sur conn terminée : retourne `-EPIPE` (non spécifié dans le header ; `0`/`TCP_ETXFULL` seulement pour le cas nominal).
- I6 [net/tcp] `tcp_peer_addr` IPv6 : format `[addr]:port` (crochets), IPv4 `addr:port`. NULL si getpeername échoue.
- I7 [net/tcp] `tcp_close(graceful=1)` : tx vidé puis `shutdown(SHUT_WR)`, on_close(0) immédiat ensuite, sans attendre le FIN du pair. Si rx non lu subsiste à la fermeture, le noyau peut envoyer RST et tronquer la fin côté pair. À revoir si le protocole exige un FIN acquitté.
- I8 [net/tcp] `tcp_listen` / `tcp_connect` : errno posé en cas d'échec (EINVAL pour hôte non littéral). `tcp_connect` retourne NULL seulement pour échec immédiat (connect ≠ EINPROGRESS) ; ECONNREFUSED asynchrone via on_close.
- I9 [net/tcp] Fallback si `loop_timer_add` échoue (OOM) : on_close appelé de façon synchrone, violant « jamais en synchrone ». Journalisé LOG_E. Cas non testé.
- I10 [core/loop] (ex-I4 écrasé) thread-safety de loop_set_wake non précisée → non thread-safe, appeler avant loop_run.
- I11 [core/loop] (ex-I5) loop_stop avant loop_run : remis à zéro par loop_run.
- I12 [core/loop] (ex-I6) fd fermé sans loop_del_fd : entrée périmée ; règle = toujours loop_del_fd avant close.
I7 [proto/msg] HELLO name_len 0 et MSG text_len 0 rejetés (MSG_EMALFORMED), à l'encodage comme au décodage : le header n'exige que les bornes hautes ; seul BYE déclare « 0 autorisé ». Interprétation stricte choisie, à confirmer si un message vide doit être accepté.
I8 [proto/msg] Encodage BYE : la longueur vient de `u.bye.len` (authoritative, comme MSG), pas de strlen ; reason_len > MSG_REASON_MAX ou UTF-8/NUL invalide → erreur. HELLO name reste en C-string (pas de champ len dans le header).
- Arbitrage Opus : I7 msg vide/nom vide refusés = accepté (l UI ne propose pas l envoi vide).
- I? [store/history] hist_set_status : header ne précise pas le retour si message absent. Choix : 0 = mis à jour, 1 = aucune ligne (message absent), <0 erreur. Documenté en tête de history.c.
- I? [net/discovery] Brief « nom > 64 tronqué → LOG_W + ignoré » ambigu : interprété en rejet de la ligne (pas de troncature) dans disc_static.c. Le nom mDNS publié est tronqué à 63 octets (label DNS), TXT name complet.
- I? [sec/session] noise-c n'expose pas noise_cipherstate_rekey (grep include/ et src/) alors que le header et la spec demandent un rekey. Émulation Noise §4.2 dans sess_noise.c : set_nonce(2^64-2), chiffrement de 32 octets nuls, init_key sur les 32 premiers, set_nonce(compteur). Déclenchée à count % SESS_REKEY_EVERY == 0 des deux côtés.
- I? [sec/session] Vecteur cacophony Noise_XX (cacophony.txt) sans handshake_hash : seul noise-c-basic.txt compare le hash. Test : égalité des hash initiateur/répondeur dans le cas cacophony.
- I? [sec/session] tcp_send renvoie TCP_ETXFULL sans drain callback, et un chiffrement consomme un nonce : choix d'une file txq de trames déjà chiffrées, vidée par timer de 1 ms (ordre des nonces préservé). sess_send refuse avec TCP_ETXFULL avant chiffrement si la file dépasse TCP_TX_MAX.
- Revue Opus (sec/session) : rekey émulé accepté. Écart à Noise §11.3 (nonce 2^64-2 au lieu de 2^64-1, refusé par noise-c), compatible uniquement entre instances p2pmsg. Nonce unique sous l ancienne clé, pas de réutilisation. Primitives noise-c seulement.
- I? [peer/manager] Règle de double session : garder la session dont la clé initiatrice est memcmp-la plus petite ; l'initiateur est la clé locale pour un lien sortant, la clé du pair pour un lien entrant.
- I? [peer/manager] Horloge de retransmission : mesurée depuis le dernier envoi effectif, et seulement quand le pair est connecté ; 5 envois max puis on_failed.
- I? [peer/manager] Rejet d'un message au texte vide : non spécifié dans manager.h, traité comme message valide (aucun rejet).
- I? [peer/manager] time_div : les délais de la spec (1 s backoff, 10 s retry, 15/45 s keepalive) sont divisés par time_div dans les tests (UTIL_DIV=100), supposé équivalent en production avec time_div=1.
- I? [peer/manager] pm_free (src/peer/manager/pm.c:151) appelle sess_close sans laisser la boucle tourner : sessions entrantes (pm_on_accept), conn tcp et buffers restent alloués après loop_run -> ASan/valgrind fuite dans tests/app/api/test_resume (1541 o, 9 allocs). Attendu : pm_free draine ou la boucle doit tourner après fermeture. Bloque le gate app/api (asan + valgrind).
- I? [app/api] app_history retourne 0 (pas un compte) : le compte se mesure dans le callback ; confirmé dans test_history.
- I? [mesures] `cmake --build build/release -j2` échoue (exit 2) : tests/sec/session/test_identity.c:25 snprintf "%s/%s" → -Werror=format-truncation=, sous -O2 (preset release). Le binaire p2pmsg se construit seul (--target p2pmsg).
- I? [mesures] N1 RTT envoi→STATUS delivered ≈ 50 ms (médiane et p99 quasi égaux, seuil 5 ms). Cause probable : pm.c:219 pm_send ne pompe pas la file (pm_queue_push seul) ; la file n'est vidée que par tick_links → pm_queue_pump, tick à PM_TICK_MS=50 (pm.c:11, pm.c:133). Confirmé par mesure N1 (scripts/bench/n1_rtt.py), à corriger côté peer/manager.
- I? [peer/manager] N1 : un RTT aberrant de 3446 ms sur 1000 (p99 2,2 ms). Cause non investiguée.
