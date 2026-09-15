#ifndef AUDIOHAWK_GTK_SETTINGS_PAGE_H
#define AUDIOHAWK_GTK_SETTINGS_PAGE_H

#include "audiohawk/core.h"

#include <adwaita.h>
#include <gtk/gtk.h>

GtkWidget *ah_gtk_settings_page_new(AhCore *core, AdwToastOverlay *toasts);

#endif
