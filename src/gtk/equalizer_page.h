#ifndef AUDIOHAWK_GTK_EQUALIZER_PAGE_H
#define AUDIOHAWK_GTK_EQUALIZER_PAGE_H

#include "audiohawk/core.h"

#include <adwaita.h>
#include <gtk/gtk.h>

GtkWidget *ah_gtk_equalizer_page_new(AhCore *core, AdwToastOverlay *toasts);

/* Rebuild sliders/curve after external core changes. */
void ah_gtk_equalizer_page_reload(GtkWidget *page);

#endif
