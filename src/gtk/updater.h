#ifndef AUDIOHAWK_GTK_UPDATER_H
#define AUDIOHAWK_GTK_UPDATER_H

#include <gtk/gtk.h>

/* Header-bar button that shows the update status. It checks GitHub once when
 * created and installs the newest release on click, then calls quit so the
 * relaunched copy takes over. */
GtkWidget *ah_updater_new(GApplication *app, void (*quit)(gpointer), gpointer quit_data);

#endif
