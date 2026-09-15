#ifndef AUDIOHAWK_GTK_TRAY_H
#define AUDIOHAWK_GTK_TRAY_H

#include <gtk/gtk.h>

typedef struct AhTray AhTray;

typedef void (*AhTrayShowFn)(gpointer user_data);
typedef void (*AhTrayQuitFn)(gpointer user_data);

/* StatusNotifierItem tray (KDE + GNOME AppIndicator extension). */
AhTray *ah_tray_create(GtkApplication *app,
                       AhTrayShowFn on_show,
                       AhTrayQuitFn on_quit,
                       gpointer user_data);
void ah_tray_destroy(AhTray *tray);

#endif
