#ifndef AUDIOHAWK_GTK_DASHBOARD_H
#define AUDIOHAWK_GTK_DASHBOARD_H

#include "audiohawk/core.h"

#include <adwaita.h>
#include <gtk/gtk.h>

/* Preferences-style dashboard content (AdwPreferencesPage). */
GtkWidget *ah_gtk_dashboard_new(AhCore *core, AdwToastOverlay *toasts);

#endif
