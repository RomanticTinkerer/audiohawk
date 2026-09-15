/*
 * AudioHawk launcher — pick GTK or Qt UI from the desktop environment.
 * GTK: GNOME, Cinnamon, MATE, XFCE, Budgie, Pantheon, Unity, …
 * Qt:  KDE Plasma, LXQt, Deepin, Trinity, …
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { UI_GTK, UI_QT } UiToolkit;

static void lower_inplace(char *s)
{
    for (; *s; ++s)
        *s = (char)tolower((unsigned char)*s);
}

static int contains_token(const char *hay, const char *needle)
{
    return hay && needle && strstr(hay, needle) != NULL;
}

static UiToolkit detect_toolkit(void)
{
    const char *force = getenv("AUDIOHAWK_UI");
    if (force) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", force);
        lower_inplace(buf);
        if (contains_token(buf, "qt") || contains_token(buf, "kde"))
            return UI_QT;
        if (contains_token(buf, "gtk") || contains_token(buf, "gnome"))
            return UI_GTK;
    }

    char desktop[256] = {0};
    const char *xdg = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_DESKTOP");
    const char *desktop_session = getenv("DESKTOP_SESSION");
    snprintf(desktop, sizeof(desktop), "%s:%s:%s",
             xdg ? xdg : "",
             session ? session : "",
             desktop_session ? desktop_session : "");
    lower_inplace(desktop);

    if (contains_token(desktop, "kde") ||
        contains_token(desktop, "plasma") ||
        contains_token(desktop, "lxqt") ||
        contains_token(desktop, "deepin") ||
        contains_token(desktop, "trinity"))
        return UI_QT;

    if (contains_token(desktop, "gnome") ||
        contains_token(desktop, "cinnamon") ||
        contains_token(desktop, "mate") ||
        contains_token(desktop, "xfce") ||
        contains_token(desktop, "budgie") ||
        contains_token(desktop, "pantheon") ||
        contains_token(desktop, "unity") ||
        contains_token(desktop, "enlightenment") ||
        contains_token(desktop, "lxde"))
        return UI_GTK;

    /* Heuristic fallback: prefer GTK on unknown Linux desktops. */
    return UI_GTK;
}

static int try_exec(const char *path, char **argv)
{
    if (access(path, X_OK) != 0)
        return -1;
    execv(path, argv);
    return -1;
}

int main(int argc, char **argv)
{
    UiToolkit ui = detect_toolkit();

    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    char dir[4096] = ".";
    if (n > 0) {
        self[n] = '\0';
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = '\0';
            snprintf(dir, sizeof(dir), "%s", self);
        }
    }

    char gtk_path[4200];
    char qt_path[4200];
    snprintf(gtk_path, sizeof(gtk_path), "%s/audiohawk-gtk", dir);
    snprintf(qt_path, sizeof(qt_path), "%s/audiohawk-qt", dir);

    /* Forward argv (e.g. --background) to the UI binary. */
    char **child_argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!child_argv)
        return 1;
    for (int i = 1; i < argc; ++i)
        child_argv[i] = argv[i];

    if (ui == UI_QT) {
        child_argv[0] = qt_path;
        if (try_exec(qt_path, child_argv) == 0) {
            free(child_argv);
            return 0;
        }
        child_argv[0] = gtk_path;
        if (try_exec(gtk_path, child_argv) == 0) {
            free(child_argv);
            return 0;
        }
    } else {
        child_argv[0] = gtk_path;
        if (try_exec(gtk_path, child_argv) == 0) {
            free(child_argv);
            return 0;
        }
        child_argv[0] = qt_path;
        if (try_exec(qt_path, child_argv) == 0) {
            free(child_argv);
            return 0;
        }
    }

    free(child_argv);

    /* PATH fallback — keep original argv so flags survive. */
    if (ui == UI_QT) {
        execvp("audiohawk-qt", argv);
        execvp("audiohawk-gtk", argv);
    } else {
        execvp("audiohawk-gtk", argv);
        execvp("audiohawk-qt", argv);
    }

    fprintf(stderr,
            "AudioHawk: no UI binary found.\n"
            "Build with CMake, or set AUDIOHAWK_UI=gtk|qt after installing.\n");
    return 127;
}
