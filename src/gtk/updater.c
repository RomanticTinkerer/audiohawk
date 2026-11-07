#define _GNU_SOURCE /* strverscmp */
#include "updater.h"

#include <string.h>

#ifndef AUDIOHAWK_VERSION
#define AUDIOHAWK_VERSION ""
#endif

#define AH_REPO "RomanticTinkerer/audiohawk"
#define AH_LATEST_API "https://api.github.com/repos/" AH_REPO "/releases/latest"
/* install.sh picks the .deb, .rpm, AppImage, or tarball for this system. */
#define AH_INSTALL_CMD \
    "curl -fsSL https://github.com/" AH_REPO "/releases/latest/download/install.sh | sh"
/* Shipped by the .deb/.rpm with a polkit rule that skips the password prompt. */
#define AH_HELPER "/usr/lib/audiohawk/audiohawk-update"
#define AH_RETRY_SECONDS 60

typedef struct {
    GtkButton *btn;
    GApplication *app;
    void (*quit)(gpointer);
    gpointer quit_data;
    char *latest; /* set when GitHub has a newer version */
    gboolean busy;
} Updater;

static void check(Updater *u);
static void install(Updater *u);

static gboolean has_helper(void)
{
    return g_file_test(AH_HELPER, G_FILE_TEST_IS_EXECUTABLE);
}

/* Started hidden at login: the button isn't on screen, so say it out loud. */
static void notify_if_hidden(Updater *u, const char *body)
{
    if (gtk_widget_get_mapped(GTK_WIDGET(u->btn)))
        return;
    GNotification *n = g_notification_new("AudioHawk update available");
    g_notification_set_body(n, body);
    g_notification_set_default_action(n, "app.show");
    g_application_send_notification(u->app, "update", n);
    g_object_unref(n);
}

static void set_status(Updater *u, const char *text, const char *tip, gboolean highlight)
{
    gtk_button_set_label(u->btn, text);
    gtk_widget_set_tooltip_text(GTK_WIDGET(u->btn), tip);
    if (highlight) {
        gtk_widget_remove_css_class(GTK_WIDGET(u->btn), "flat");
        gtk_widget_add_css_class(GTK_WIDGET(u->btn), "suggested-action");
    } else {
        gtk_widget_remove_css_class(GTK_WIDGET(u->btn), "suggested-action");
        gtk_widget_add_css_class(GTK_WIDGET(u->btn), "flat");
    }
}

static void show_update(Updater *u, const char *tip)
{
    char *label = g_strdup_printf("Update to %s", u->latest);
    set_status(u, label, tip, TRUE);
    g_free(label);
}

static char *parse_tag(const char *json)
{
    const char *k = strstr(json, "\"tag_name\"");
    if (!k || !(k = strchr(k + 10, ':')) || !(k = strchr(k, '"')))
        return NULL;
    ++k;
    const char *e = strchr(k, '"');
    if (!e)
        return NULL;
    if (*k == 'v')
        ++k;
    return e > k ? g_strndup(k, (gsize)(e - k)) : NULL;
}

static void retry_check(gpointer data)
{
    Updater *u = data;
    if (!u->busy && !u->latest)
        check(u);
}

static void check_failed(Updater *u, const char *tip)
{
    set_status(u, "Update check failed", tip, FALSE);
    /* At login the network often isn't up yet. */
    g_timeout_add_seconds_once(AH_RETRY_SECONDS, retry_check, u);
}

static void on_checked(GObject *src, GAsyncResult *res, gpointer data)
{
    Updater *u = data;
    char *out = NULL;
    u->busy = FALSE;
    const gboolean ok =
        g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, &out, NULL, NULL) &&
        g_subprocess_get_successful(G_SUBPROCESS(src));
    char *tag = ok && out ? parse_tag(out) : NULL;
    g_free(out);

    if (!tag) {
        check_failed(u, "Couldn't reach GitHub. Click to try again.");
        return;
    }
    if (strverscmp(tag, AUDIOHAWK_VERSION) <= 0) {
        g_free(tag);
        char *label = g_strdup_printf("Up to date · %s", AUDIOHAWK_VERSION);
        set_status(u, label, "Click to check again", FALSE);
        g_free(label);
        return;
    }

    g_free(u->latest);
    u->latest = tag;

    /* No password needed: just do it. AUDIOHAWK_UPDATED stops a restart loop if
     * the install ever leaves the old version in place. */
    if ((g_getenv("APPIMAGE") || has_helper()) && !g_getenv("AUDIOHAWK_UPDATED")) {
        install(u);
        return;
    }

    show_update(u, "Download and install the new version from GitHub");
    char *body = g_strdup_printf("Version %s is out. Open AudioHawk to update.", u->latest);
    notify_if_hidden(u, body);
    g_free(body);
}

static void check(Updater *u)
{
    u->busy = TRUE;
    set_status(u, "Checking for updates…", NULL, FALSE);
    GSubprocess *p = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
        "curl", "-fsSL", "-m", "15", AH_LATEST_API, NULL);
    if (!p) {
        u->busy = FALSE;
        check_failed(u, "curl is missing, so AudioHawk can't check for updates");
        return;
    }
    g_subprocess_communicate_utf8_async(p, NULL, NULL, on_checked, u);
    g_object_unref(p);
}

static void on_installed(GObject *src, GAsyncResult *res, gpointer data)
{
    Updater *u = data;
    GSubprocess *p = G_SUBPROCESS(src);
    char *err = NULL;
    u->busy = FALSE;
    const gboolean ran = g_subprocess_communicate_utf8_finish(p, res, NULL, &err, NULL);

    if (!ran || !g_subprocess_get_successful(p)) {
        const int code = ran && g_subprocess_get_if_exited(p) ? g_subprocess_get_exit_status(p) : -1;
        if (err && *err)
            g_printerr("AudioHawk update failed:\n%s", err);
        /* pkexec: 126 = dialog dismissed, 127 = not authorized. */
        show_update(u, code == 126 || code == 127 ? "Update cancelled. Click to try again."
                                                  : "Update failed. Click to try again.");
        g_free(err);
        char *body = g_strdup_printf("Couldn't install version %s. Open AudioHawk to retry.",
                                     u->latest);
        notify_if_hidden(u, body);
        g_free(body);
        return;
    }
    g_free(err);

    set_status(u, "Restarting…", NULL, FALSE);
    const char *self = g_getenv("APPIMAGE");
    const gboolean hidden = !gtk_widget_get_mapped(GTK_WIDGET(u->btn));
    /* Come back the way we were: hidden at login stays hidden.
     * ponytail: fixed 2 s wait for this process to exit; wait on its pid if relaunch races. */
    char *argv[] = { "sh", "-c", "sleep 2; AUDIOHAWK_UPDATED=1 exec \"$0\" \"$@\"",
                     (char *)(self ? self : "audiohawk"), hidden ? "--background" : NULL, NULL };
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
    u->quit(u->quit_data);
}

static void install(Updater *u)
{
    u->busy = TRUE;
    set_status(u, "Updating…", "Downloading and installing from GitHub", FALSE);
    const GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE;
    /* An AppImage replaces its own file; package installs need root. */
    GSubprocess *p;
    if (g_getenv("APPIMAGE"))
        p = g_subprocess_new(flags, NULL, "/bin/sh", "-c", AH_INSTALL_CMD, NULL);
    else if (has_helper())
        p = g_subprocess_new(flags, NULL, "pkexec", AH_HELPER, NULL);
    else
        p = g_subprocess_new(flags, NULL, "pkexec", "/bin/sh", "-c", AH_INSTALL_CMD, NULL);
    if (!p) {
        u->busy = FALSE;
        show_update(u, "pkexec is missing. Run the install command from the README instead.");
        return;
    }
    g_subprocess_communicate_utf8_async(p, NULL, NULL, on_installed, u);
    g_object_unref(p);
}

static void on_clicked(GtkButton *btn, gpointer data)
{
    Updater *u = data;
    (void)btn;
    if (u->busy)
        return;
    if (u->latest)
        install(u);
    else
        check(u);
}

static void updater_free(gpointer data)
{
    Updater *u = data;
    g_free(u->latest);
    g_free(u);
}

GtkWidget *ah_updater_new(GApplication *app, void (*quit)(gpointer), gpointer quit_data)
{
    Updater *u = g_new0(Updater, 1);
    u->app = app;
    u->quit = quit;
    u->quit_data = quit_data;
    u->btn = GTK_BUTTON(gtk_button_new());
    g_object_set_data_full(G_OBJECT(u->btn), "ah-updater", u, updater_free);
    g_signal_connect(u->btn, "clicked", G_CALLBACK(on_clicked), u);
    check(u);
    return GTK_WIDGET(u->btn);
}
