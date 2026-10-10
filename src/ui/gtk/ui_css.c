/* ui/gtk — thème sombre forcé, pas d'animation, CSS embarqué (palette SPEC §7). */
#include <gtk/gtk.h>

#include "ui/gtk/ui_app.h"

static const char UI_CSS[] =
    "window { background: linear-gradient(160deg, #0c0d10, #232731); color: #e6e8ee; }\n"
    ".peer-name { color: #e6e8ee; }\n"
    ".status-dot { min-width: 10px; min-height: 10px; border-radius: 5px; margin-top: 4px; }\n"
    ".status-connected { background-color: #00d88a; }\n"
    ".status-offline { background-color: #ff4d6a; }\n"
    ".status-connecting { background-color: #f0a030; }\n"
    "listview row:selected, list row:selected, row:selected { background-color: #4a9eff; "
    "color: #0c0d10; border-radius: 6px; }\n"
    ".bubble { border-radius: 12px; padding: 8px 12px; margin: 2px 8px; }\n"
    ".bubble-out { background-color: #232731; }\n"
    ".bubble-in { background-color: #1a1d24; }\n"
    ".bubble-meta { font-size: 0.8em; color: #8a90a0; }\n"
    ".status-failed { color: #ff4d6a; font-weight: bold; }\n"
    ".input { background-color: #1a1d24; border-radius: 10px; padding: 6px; }\n"
    ".input textview, .input text { background-color: transparent; color: #e6e8ee; }\n"
    ".badge { background-color: #4a9eff; color: #0c0d10; border-radius: 9px; padding: 0 6px;"
    " font-size: 11px; font-weight: 700; }\n"
    ".identity-name { font-weight: 700; font-size: 15px; color: #e6e8ee; }\n"
    ".status-text { font-size: 12px; font-weight: 600; }\n"
    ".status-text.status-connected { background-color: transparent; color: #00d88a; }\n"
    ".status-text.status-connecting { background-color: transparent; color: #f0a030; }\n"
    ".status-text.status-offline { background-color: transparent; color: #ff4d6a; }\n"
    ".peer-name { color: #e6e8ee; }\n";

static void apply_dark_settings(void)
{
    GtkSettings *settings = gtk_settings_get_default();

    if (settings == NULL) {
        return;
    }
    g_object_set(settings,
                 "gtk-application-prefer-dark-theme", TRUE,
                 "gtk-enable-animations", FALSE,
                 NULL);
}

void ui_css_apply(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();

    apply_dark_settings();
    if (display == NULL) {
        g_object_unref(provider);
        return;
    }
    gtk_css_provider_load_from_string(provider, UI_CSS);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}
