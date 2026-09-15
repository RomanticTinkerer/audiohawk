#include "dashboard.h"
#include "equalizer_page.h"
#include "icons.h"
#include "settings_page.h"
#include "tray.h"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <string.h>

#ifndef AUDIOHAWK_SOURCE_DIR
#define AUDIOHAWK_SOURCE_DIR ""
#endif

typedef struct {
    AhCore *core;
    GtkApplication *app;
    GtkWindow *win;
    AdwWindowTitle *title;
    GtkStack *stack;
    GtkWidget *dock_btns[3];
    GtkWidget *eq_page;
    AhTray *tray;
    gboolean quitting;
    gboolean told_background;
    gboolean start_background;
} Shell;

static void load_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    char *resolved = NULL;

    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe) {
        char *dir = g_path_get_dirname(exe);
        const char *beside[] = {
            "%s/../data/css/audiohawk.css",
            "%s/data/css/audiohawk.css",
            "%s/../share/audiohawk/audiohawk.css",
            NULL
        };
        for (int i = 0; beside[i]; ++i) {
            char *path = g_strdup_printf(beside[i], dir);
            if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
                resolved = path;
                break;
            }
            g_free(path);
        }
        g_free(dir);
        g_free(exe);
    }

    if (!resolved && AUDIOHAWK_SOURCE_DIR[0] != '\0') {
        char *path = g_strdup_printf("%s/data/css/audiohawk.css", AUDIOHAWK_SOURCE_DIR);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            resolved = path;
        else
            g_free(path);
    }

    if (!resolved) {
        const char *candidates[] = {
            "data/css/audiohawk.css",
            "../data/css/audiohawk.css",
            "/usr/share/audiohawk/audiohawk.css",
            "/usr/local/share/audiohawk/audiohawk.css",
            NULL
        };
        for (int i = 0; candidates[i]; ++i) {
            if (g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR)) {
                resolved = g_strdup(candidates[i]);
                break;
            }
        }
    }

    if (resolved) {
        gtk_css_provider_load_from_path(provider, resolved);
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_free(resolved);
    }
    g_object_unref(provider);
}

static void set_active_tab(Shell *shell, int index)
{
    const char *names[] = { "home", "equalizer", "settings" };
    const char *titles[] = { "Home", "Equalizer", "Settings" };
    const char *subs[] = {
        "Listening profiles",
        "Graphic EQ & PipeWire",
        "Preferences"
    };

    if (index < 0 || index > 2)
        return;

    gtk_stack_set_visible_child_name(shell->stack, names[index]);
    adw_window_title_set_title(shell->title, titles[index]);
    adw_window_title_set_subtitle(shell->title, subs[index]);

    for (int i = 0; i < 3; ++i) {
        if (i == index)
            gtk_widget_add_css_class(shell->dock_btns[i], "active");
        else
            gtk_widget_remove_css_class(shell->dock_btns[i], "active");
    }

    if (index == 1 && shell->eq_page)
        ah_gtk_equalizer_page_reload(shell->eq_page);
}

static void on_dock_clicked(GtkButton *btn, gpointer user_data)
{
    Shell *shell = user_data;
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "tab-index"));
    set_active_tab(shell, index);
}

static GtkWidget *make_dock_button(const char *icon, const char *label, int index, Shell *shell)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "ah-dock-btn");
    gtk_widget_add_css_class(btn, "flat");
    g_object_set_data(G_OBJECT(btn), "tab-index", GINT_TO_POINTER(index));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), ah_icon_image(icon, 22));

    GtkWidget *lab = gtk_label_new(label);
    gtk_widget_add_css_class(lab, "caption");
    gtk_box_append(GTK_BOX(box), lab);

    gtk_button_set_child(GTK_BUTTON(btn), box);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_dock_clicked), shell);
    shell->dock_btns[index] = btn;
    return btn;
}

static GtkWidget *make_dock(Shell *shell)
{
    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(wrap, "ah-dock-wrap");
    gtk_widget_set_halign(wrap, GTK_ALIGN_CENTER);

    GtkWidget *dock = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(dock, "ah-dock");
    gtk_widget_set_halign(dock, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(dock), make_dock_button("nav-home", "Home", 0, shell));
    gtk_box_append(GTK_BOX(dock), make_dock_button("nav-eq", "Equalizer", 1, shell));
    gtk_box_append(GTK_BOX(dock), make_dock_button("nav-settings", "Settings", 2, shell));

    gtk_box_append(GTK_BOX(wrap), dock);
    return wrap;
}

static void show_window(Shell *shell)
{
    if (!shell || !shell->win)
        return;
    gtk_window_present(shell->win);
}

static void quit_app(Shell *shell)
{
    if (!shell)
        return;
    shell->quitting = TRUE;
    if (shell->tray) {
        ah_tray_destroy(shell->tray);
        shell->tray = NULL;
    }
    if (shell->core)
        ah_core_persist(shell->core);
    if (shell->app)
        g_application_quit(G_APPLICATION(shell->app));
}

static void tray_show(gpointer user_data)
{
    show_window(user_data);
}

static void tray_quit(gpointer user_data)
{
    quit_app(user_data);
}

static void on_show_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action;
    (void)param;
    show_window(user_data);
}

static void on_quit_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action;
    (void)param;
    quit_app(user_data);
}

static gboolean on_close_request(GtkWindow *window, gpointer user_data)
{
    Shell *shell = user_data;
    (void)window;

    if (shell->quitting)
        return FALSE; /* allow destroy / quit */

    /* Keep PipeWire EQ alive — only hide the UI (tray stays interactive). */
    gtk_widget_set_visible(GTK_WIDGET(shell->win), FALSE);

    if (!shell->told_background) {
        shell->told_background = TRUE;
        g_printerr("AudioHawk: running in the background (EQ stays active). "
                   "Use the tray icon, launch AudioHawk again, or Quit from the menu.\n");
    }
    return TRUE;
}

static gboolean start_background_flag = FALSE;
static gboolean quit_flag = FALSE;

static GOptionEntry ah_options[] = {
    { "background", 'b', 0, G_OPTION_ARG_NONE, &start_background_flag,
      "Start hidden in the background (for autostart)", NULL },
    { "minimized", 0, 0, G_OPTION_ARG_NONE, &start_background_flag,
      "Alias for --background", NULL },
    { "quit", 0, 0, G_OPTION_ARG_NONE, &quit_flag,
      "Quit a running AudioHawk instance", NULL },
    { NULL }
};

static void apply_window_icon(GtkWindow *win)
{
    const char *paths[] = {
        "/usr/share/icons/hicolor/128x128/apps/audiohawk.png",
        "/usr/local/share/icons/hicolor/128x128/apps/audiohawk.png",
        NULL
    };
    for (int i = 0; paths[i]; ++i) {
        if (!g_file_test(paths[i], G_FILE_TEST_IS_REGULAR))
            continue;
        gtk_window_set_icon_name(win, "audiohawk");
        return;
    }

    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (!exe)
        return;
    char *dir = g_path_get_dirname(exe);
    char *png = g_strdup_printf("%s/../data/icons/hicolor/128x128/apps/audiohawk.png", dir);
    char *norm = g_canonicalize_filename(png, NULL);
    g_free(png);
    g_free(dir);
    g_free(exe);
    if (norm && g_file_test(norm, G_FILE_TEST_IS_REGULAR))
        gtk_window_set_icon_name(win, "audiohawk");
    g_free(norm);
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    Shell *shell = user_data;

    /* Options are parsed before activate; honor --background / --minimized. */
    if (start_background_flag)
        shell->start_background = TRUE;

    /* Second launch / activate: bring the existing window back. */
    if (shell->win) {
        show_window(shell);
        return;
    }

    shell->app = app;

    adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
                                       ADW_COLOR_SCHEME_DEFAULT);
    load_css();

    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
    shell->win = GTK_WINDOW(win);
    gtk_window_set_title(shell->win, "AudioHawk");
    gtk_window_set_default_size(shell->win, 720, 820);
    apply_window_icon(shell->win);
    g_signal_connect(shell->win, "close-request", G_CALLBACK(on_close_request), shell);

    if (!shell->tray)
        shell->tray = ah_tray_create(app, tray_show, tray_quit, shell);

    AdwToastOverlay *toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    AdwToolbarView *toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());

    AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());
    shell->title = ADW_WINDOW_TITLE(adw_window_title_new("Home", "Listening profiles"));
    adw_header_bar_set_title_widget(header, GTK_WIDGET(shell->title));

    /* Menu: reopen is via launcher; Quit fully stops background processing. */
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Show AudioHawk", "app.show");
    g_menu_append(menu, "Quit AudioHawk", "app.quit");
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(menu));
    adw_header_bar_pack_end(header, menu_btn);
    g_object_unref(menu);

    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

    shell->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(shell->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    GtkWidget *home = ah_gtk_dashboard_new(shell->core, toasts);
    shell->eq_page = ah_gtk_equalizer_page_new(shell->core, toasts);
    GtkWidget *settings = ah_gtk_settings_page_new(shell->core, toasts);

    gtk_stack_add_named(shell->stack, home, "home");
    gtk_stack_add_named(shell->stack, shell->eq_page, "equalizer");
    gtk_stack_add_named(shell->stack, settings, "settings");

    adw_toolbar_view_set_content(toolbar, GTK_WIDGET(shell->stack));
    adw_toolbar_view_add_bottom_bar(toolbar, make_dock(shell));

    adw_toast_overlay_set_child(toasts, GTK_WIDGET(toolbar));
    adw_application_window_set_content(win, GTK_WIDGET(toasts));

    set_active_tab(shell, 0);

    if (shell->start_background) {
        /* Autostart / --background: keep EQ running without flashing the UI. */
        gtk_widget_set_visible(GTK_WIDGET(shell->win), FALSE);
        shell->told_background = TRUE;
    } else {
        gtk_window_present(shell->win);
    }
}

int main(int argc, char **argv)
{
    AhCore core;
    if (ah_core_init(&core) != 0) {
        g_printerr("AudioHawk: failed to initialize core\n");
        return 1;
    }

    Shell shell = {0};
    shell.core = &core;

    AdwApplication *app = adw_application_new("dev.audiohawk.app",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_application_add_main_option_entries(G_APPLICATION(app), ah_options);

    const GActionEntry entries[] = {
        { .name = "show", .activate = on_show_action },
        { .name = "quit", .activate = on_quit_action },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), entries,
                                    G_N_ELEMENTS(entries), &shell);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &shell);

    /* Parse --quit early so a second process can ask the primary to exit. */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--quit") == 0)
            quit_flag = TRUE;
    }
    if (quit_flag) {
        GError *err = NULL;
        if (g_application_register(G_APPLICATION(app), NULL, &err)) {
            if (g_application_get_is_remote(G_APPLICATION(app)))
                g_action_group_activate_action(G_ACTION_GROUP(app), "quit", NULL);
        } else {
            g_clear_error(&err);
        }
        g_object_unref(app);
        ah_core_shutdown(&core);
        return 0;
    }

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    if (shell.tray) {
        ah_tray_destroy(shell.tray);
        shell.tray = NULL;
    }
    g_object_unref(app);
    ah_core_shutdown(&core);
    return status;
}
