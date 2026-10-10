/* ui/gtk — contexte partagé et contrats internes (bridge, fenêtre, conversation ; thread principal sauf bridge). */
#ifndef P2P_UI_GTK_APP_H
#define P2P_UI_GTK_APP_H
#include <gtk/gtk.h>

#include "app/api/app.h"
#include "ui/gtk/ui_model.h"
#include "ui/gtk/ui_options.h"

#define UI_APP_ID "org.p2pmsg.gtk"
#define UI_PATH_MAX 512
#define UI_NAME_MAX 65

typedef struct {
    app_t          *app;
    ui_model        model;
    GtkWidget      *window;
    GtkWidget      *peer_list;
    GtkWidget      *conv_list;
    GtkWidget      *conv_scroll;
    GtkTextBuffer  *input_buf;
    GtkWidget      *input_view;
    GtkWidget      *input_box;
    GtkWidget      *input_hint;
    GtkWidget      *conv_title;
    GtkWidget      *conv_status;
    GtkWidget      *peer_hint;
    GtkWidget      *conv_hint;
    char            peers_file[UI_PATH_MAX];
    char            self_name[UI_NAME_MAX];
    int             shutting;
    int             started;
    int             failed;
} ui_ctx;

typedef enum { UI_EV_PEER, UI_EV_MESSAGE, UI_EV_STATUS } ui_event_kind;

/* Copie possédée d'un évènement réseau, livrée au thread GTK par g_idle_add. */
typedef struct {
    ui_ctx         *ctx;
    ui_event_kind   kind;
    app_peer        peer;
    app_message     msg;    /* msg.text pointe sur text ; STATUS : msg.peer, msg.id, msg.status */
    char           *text;
} ui_event;

/* bridge : app_new (0 ou -1, raison loggée par app), app_start / app_stop / app_free. */
int  ui_bridge_open(ui_ctx *ctx, const ui_options *opts);
int  ui_bridge_start(ui_ctx *ctx);
void ui_bridge_stop(ui_ctx *ctx);
void ui_bridge_close(ui_ctx *ctx);

/* window : appelé sur le thread principal. */
void ui_window_on_activate(GtkApplication *gapp, gpointer ud);
void ui_window_on_shutdown(GtkApplication *gapp, gpointer ud);
void ui_window_apply(ui_ctx *ctx, const ui_event *ev);
void ui_window_add_peer_row(ui_ctx *ctx, int idx);
void ui_window_refresh_peer(ui_ctx *ctx, int idx);
void ui_window_select_peer_idx(ui_ctx *ctx, int idx);

/* conv : conversation courante (modèle + lignes GtkListBox). */
GtkWidget *ui_conv_build_pane(ui_ctx *ctx);
void ui_conv_select(ui_ctx *ctx, const char *key);
void ui_conv_append(ui_ctx *ctx, const app_message *msg);
void ui_conv_set_status(ui_ctx *ctx, const char *peer, uint64_t id, app_msg_status st);
void ui_conv_refresh_header(ui_ctx *ctx);

/* css : CSS embarqué + thème sombre, appliqué une fois. */
void ui_css_apply(void);

#endif
