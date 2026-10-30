#ifndef AUDIOHAWK_GTK_ADVANCED_PAGE_H
#define AUDIOHAWK_GTK_ADVANCED_PAGE_H

#include "audiohawk/core.h"

#include <adwaita.h>
#include <gtk/gtk.h>

GtkWidget *ah_gtk_advanced_page_new(AhCore *core, AdwToastOverlay *toasts);

#endif
