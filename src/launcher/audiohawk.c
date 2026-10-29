/*
 * AudioHawk launcher — start the GTK UI next to this binary, else PATH.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
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
    snprintf(gtk_path, sizeof(gtk_path), "%s/audiohawk-gtk", dir);

    char **child_argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!child_argv)
        return 1;
    child_argv[0] = gtk_path;
    for (int i = 1; i < argc; ++i)
        child_argv[i] = argv[i];

    execv(gtk_path, child_argv);
    execvp("audiohawk-gtk", argv);
    free(child_argv);
    fprintf(stderr, "AudioHawk: GTK UI not found. Build with CMake.\n");
    return 127;
}
