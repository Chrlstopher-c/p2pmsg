/* core/log — logging structuré : niveaux, sortie stderr + fichier optionnel. Thread-safe. */
#ifndef P2P_CORE_LOG_H
#define P2P_CORE_LOG_H

typedef enum { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR } log_level;

/* path NULL = stderr seul. Fichier ouvert en troncature (reset au démarrage), 0600. 0 ou -errno. */
int  log_init(log_level min, const char *path);
void log_shutdown(void);
void log_set_level(log_level min);

/* Format d'une ligne : "2026-10-10T12:00:00.123Z LEVEL module: message key=value...\n"
 * module = chaîne courte non NULL. fmt printf. Ligne tronquée à 1024 octets. */
void log_write(log_level lvl, const char *module, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define LOG_D(mod, ...) log_write(LOG_DEBUG, mod, __VA_ARGS__)
#define LOG_I(mod, ...) log_write(LOG_INFO, mod, __VA_ARGS__)
#define LOG_W(mod, ...) log_write(LOG_WARN, mod, __VA_ARGS__)
#define LOG_E(mod, ...) log_write(LOG_ERROR, mod, __VA_ARGS__)

#endif
