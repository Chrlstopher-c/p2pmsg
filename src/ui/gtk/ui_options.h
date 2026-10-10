/* ui/gtk — options de ligne de commande (mêmes que p2pmsg CLI). */
#ifndef P2P_UI_GTK_OPTIONS_H
#define P2P_UI_GTK_OPTIONS_H
#include <stdint.h>

typedef struct {
    const char *dir;
    const char *name;
    uint16_t    port;
    int         no_mdns;
    int         debug;
} ui_options;

/* 0 ou -EINVAL (option inconnue, valeur invalide). */
int ui_options_parse(int argc, char **argv, ui_options *out);

#endif
