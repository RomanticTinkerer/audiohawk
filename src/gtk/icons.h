#ifndef AUDIOHAWK_GTK_ICONS_H
#define AUDIOHAWK_GTK_ICONS_H

#include <gtk/gtk.h>

/* Load a shipped B&W SVG from data/icons/<name>.svg */
GtkWidget *ah_icon_image(const char *name, int pixel_size);

#endif
