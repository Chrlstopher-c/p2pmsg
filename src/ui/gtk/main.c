/* ui/gtk — binaire p2pmsg-gtk : options CLI, app_new, GtkApplication non unique (deux instances par machine). */
#include <gtk/gtk.h>
#include <stdio.h>

#include "ui/gtk/ui_app.h"

static const char USAGE[] = "usage : p2pmsg-gtk [--dir D] [--name N] [--port P] [--no-mdns] [--debug]\n";

int main(int argc, char **argv)
{
    ui_ctx ctx = {0};
    ui_options opts;
    GtkApplication *gapp;
    char *gargv[] = {argv[0], NULL};
    int rc;

    if (ui_options_parse(argc, argv, &opts) != 0) {
        fputs(USAGE, stderr);
        return 2;
    }
    if (ui_bridge_open(&ctx, &opts) != 0) {
        return 1;
    }
    gapp = gtk_application_new(UI_APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gapp, "activate", G_CALLBACK(ui_window_on_activate), &ctx);
    g_signal_connect(gapp, "shutdown", G_CALLBACK(ui_window_on_shutdown), &ctx);
    rc = g_application_run(G_APPLICATION(gapp), 1, gargv);
    g_object_unref(gapp);
    ui_bridge_close(&ctx);
    return ctx.failed ? 1 : rc;
}
