#include "icons.h"

#include <adwaita.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef AUDIOHAWK_SOURCE_DIR
#define AUDIOHAWK_SOURCE_DIR ""
#endif

static gboolean try_path(char *out, size_t out_len, const char *fmt, ...)
{
    char candidate[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(candidate, sizeof(candidate), fmt, ap);
    va_end(ap);

    if (!g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
        return FALSE;

    snprintf(out, out_len, "%s", candidate);
    return TRUE;
}

static gboolean resolve_icon(const char *name, char *out, size_t out_len)
{
    if (!name || !name[0])
        return FALSE;

    /* 1) Absolute paths relative to the running binary (works regardless of CWD). */
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe) {
        char *dir = g_path_get_dirname(exe);
        /* Dev layout: <prefix>/build/audiohawk-gtk → <prefix>/data/icons */
        if (try_path(out, out_len, "%s/../data/icons/%s.svg", dir, name) ||
            try_path(out, out_len, "%s/data/icons/%s.svg", dir, name) ||
            /* Install layout: <prefix>/bin/audiohawk-gtk → <prefix>/share/audiohawk/icons */
            try_path(out, out_len, "%s/../share/audiohawk/icons/%s.svg", dir, name)) {
            g_free(dir);
            g_free(exe);
            return TRUE;
        }
        g_free(dir);
        g_free(exe);
    }

    /* 2) Compile-time source tree (cmake -DAUDIOHAWK_SOURCE_DIR=...). */
    if (AUDIOHAWK_SOURCE_DIR[0] != '\0' &&
        try_path(out, out_len, "%s/data/icons/%s.svg", AUDIOHAWK_SOURCE_DIR, name))
        return TRUE;

    /* 3) System install. */
    if (try_path(out, out_len, "/usr/share/audiohawk/icons/%s.svg", name) ||
        try_path(out, out_len, "/usr/local/share/audiohawk/icons/%s.svg", name))
        return TRUE;

    /* 4) CWD-relative (running from repo root or build/). */
    if (try_path(out, out_len, "data/icons/%s.svg", name) ||
        try_path(out, out_len, "../data/icons/%s.svg", name) ||
        try_path(out, out_len, "../../data/icons/%s.svg", name))
        return TRUE;

    return FALSE;
}

static void cleanup_tmp_dir(gpointer data)
{
    char *dir = data;
    if (!dir)
        return;
    char *svg = g_build_filename(dir, "icon.svg", NULL);
    unlink(svg);
    g_free(svg);
    rmdir(dir);
    g_free(dir);
}

GtkWidget *ah_icon_image(const char *name, int pixel_size)
{
    char path[1024];
    int size = pixel_size > 0 ? pixel_size : 20;
    GtkWidget *image;

    if (!resolve_icon(name, path, sizeof(path))) {
        g_warning("AudioHawk: missing icon '%s'", name ? name : "(null)");
        image = gtk_image_new_from_icon_name("image-missing-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(image), size);
        gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
        return image;
    }

    char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &contents, &len, NULL)) {
        image = gtk_image_new_from_file(path);
        gtk_image_set_pixel_size(GTK_IMAGE(image), size);
        gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
        return image;
    }

    /* Lucide SVGs use currentColor — paint pure black or white for the theme. */
    const char *ink = adw_style_manager_get_dark(adw_style_manager_get_default())
                          ? "#ffffff"
                          : "#000000";

    GString *gs = g_string_new(NULL);
    const char *cursor = contents;
    const char *hit;
    while ((hit = strstr(cursor, "currentColor")) != NULL) {
        g_string_append_len(gs, cursor, hit - cursor);
        g_string_append(gs, ink);
        cursor = hit + strlen("currentColor");
    }
    g_string_append(gs, cursor);
    g_free(contents);

    char *tmp_dir = g_dir_make_tmp("audiohawk-icons-XXXXXX", NULL);
    if (!tmp_dir) {
        g_string_free(gs, TRUE);
        image = gtk_image_new_from_file(path);
        gtk_image_set_pixel_size(GTK_IMAGE(image), size);
        gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
        return image;
    }

    char *tmp = g_build_filename(tmp_dir, "icon.svg", NULL);
    if (!g_file_set_contents(tmp, gs->str, (gssize)gs->len, NULL)) {
        g_string_free(gs, TRUE);
        g_free(tmp);
        cleanup_tmp_dir(tmp_dir);
        image = gtk_image_new_from_file(path);
        gtk_image_set_pixel_size(GTK_IMAGE(image), size);
        gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
        return image;
    }
    g_string_free(gs, TRUE);

    image = gtk_image_new_from_file(tmp);
    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    gtk_widget_set_valign(image, GTK_ALIGN_CENTER);

    g_free(tmp);
    g_object_set_data_full(G_OBJECT(image), "ah-tmp-dir", tmp_dir, cleanup_tmp_dir);

    return image;
}
