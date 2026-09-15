#include "equalizer_page.h"
#include "icons.h"

#include <math.h>
#include <string.h>

typedef struct {
    AhCore *core;
    GtkWidget *page;
    AdwComboRow *preset_combo;
    AdwActionRow *band_rows[3];
    GtkWidget *band_checks[3];
    AdwActionRow *view_rows[2];
    GtkWidget *view_checks[2];
    GtkWidget *view_stack;
    GtkWidget *sliders_box;
    GtkWidget *curve_area;
    GtkWidget *scales[AH_EQ_BANDS_MAX];
    gboolean suppress;
} EqPage;

static void sync_checks(GtkWidget **checks, int count, int active)
{
    for (int i = 0; i < count; ++i)
        gtk_widget_set_opacity(checks[i], i == active ? 1.0 : 0.0);
}

static const char *fmt_freq(float hz, char *buf, size_t n)
{
    if (hz >= 1000.f)
        snprintf(buf, n, "%gk", hz / 1000.f);
    else
        snprintf(buf, n, "%.0f", hz);
    return buf;
}

static void draw_curve(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer user_data)
{
    EqPage *p = user_data;
    (void)area;

    gboolean dark = adw_style_manager_get_dark(adw_style_manager_get_default());
    if (dark)
        cairo_set_source_rgb(cr, 0.16, 0.16, 0.18);
    else
        cairo_set_source_rgb(cr, 0.96, 0.96, 0.97);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    cairo_set_line_width(cr, 1.0);
    if (dark)
        cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
    else
        cairo_set_source_rgba(cr, 0, 0, 0, 0.08);

    double mid = height * 0.5;
    cairo_move_to(cr, 0, mid);
    cairo_line_to(cr, width, mid);
    cairo_stroke(cr);

    int n = (int)p->core->equalizer.band_count;
    if (n < 2)
        return;

    /* Accent-ish stroke that stays readable on light/dark surfaces. */
    if (dark)
        cairo_set_source_rgba(cr, 0.45, 0.72, 1.0, 0.95);
    else
        cairo_set_source_rgba(cr, 0.15, 0.45, 0.90, 0.95);
    cairo_set_line_width(cr, 2.5);

    for (int i = 0; i < n; ++i) {
        double x = (double)i / (double)(n - 1) * (width - 24.0) + 12.0;
        double g = p->core->equalizer.bands[i].gain_db;
        double y = mid - (g / 12.0) * (height * 0.42);
        if (i == 0)
            cairo_move_to(cr, x, y);
        else
            cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    for (int i = 0; i < n; ++i) {
        double x = (double)i / (double)(n - 1) * (width - 24.0) + 12.0;
        double g = p->core->equalizer.bands[i].gain_db;
        double y = mid - (g / 12.0) * (height * 0.42);
        cairo_arc(cr, x, y, 3.5, 0, G_PI * 2.0);
        cairo_fill(cr);
    }
}

static void on_scale_changed(GtkRange *range, gpointer user_data)
{
    EqPage *p = user_data;
    if (p->suppress)
        return;

    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "band-index"));
    float gain = (float)gtk_range_get_value(range);
    ah_core_set_band_gain(p->core, idx, gain);

    GtkWidget *glab = g_object_get_data(G_OBJECT(range), "gain-label");
    if (glab) {
        char gbuf[16];
        snprintf(gbuf, sizeof(gbuf), "%+.0f", gain);
        gtk_label_set_text(GTK_LABEL(glab), gbuf);
    }

    if (p->preset_combo) {
        p->suppress = TRUE;
        adw_combo_row_set_selected(p->preset_combo, AH_EQ_PRESET_CUSTOM);
        p->suppress = FALSE;
    }
    gtk_widget_queue_draw(p->curve_area);
}

static void rebuild_sliders(EqPage *p)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(p->sliders_box)) != NULL)
        gtk_box_remove(GTK_BOX(p->sliders_box), child);

    memset(p->scales, 0, sizeof(p->scales));
    int n = (int)p->core->equalizer.band_count;
    p->suppress = TRUE;

    for (int i = 0; i < n; ++i) {
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_hexpand(col, TRUE);

        char fbuf[32];
        fmt_freq(p->core->equalizer.bands[i].freq_hz, fbuf, sizeof(fbuf));
        GtkWidget *lab = gtk_label_new(fbuf);
        gtk_widget_add_css_class(lab, "caption");
        gtk_box_append(GTK_BOX(col), lab);

        GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, -12.0, 12.0, 0.5);
        gtk_range_set_inverted(GTK_RANGE(scale), TRUE);
        gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
        gtk_widget_set_vexpand(scale, TRUE);
        gtk_widget_set_size_request(scale, -1, 160);
        gtk_range_set_value(GTK_RANGE(scale), p->core->equalizer.bands[i].gain_db);
        g_object_set_data(G_OBJECT(scale), "band-index", GINT_TO_POINTER(i));

        char gbuf[16];
        snprintf(gbuf, sizeof(gbuf), "%+.0f", p->core->equalizer.bands[i].gain_db);
        GtkWidget *gains = gtk_label_new(gbuf);
        gtk_widget_add_css_class(gains, "caption");
        g_object_set_data(G_OBJECT(scale), "gain-label", gains);

        g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_changed), p);
        p->scales[i] = scale;

        gtk_box_append(GTK_BOX(col), scale);
        gtk_box_append(GTK_BOX(col), gains);
        gtk_box_append(GTK_BOX(p->sliders_box), col);
    }

    p->suppress = FALSE;
    gtk_widget_queue_draw(p->curve_area);
}

static int band_count_index(AhBandCount c)
{
    if (c == AH_BANDS_15)
        return 1;
    if (c == AH_BANDS_20)
        return 2;
    return 0;
}

static void refresh_selection(EqPage *p)
{
    p->suppress = TRUE;
    if (p->preset_combo)
        adw_combo_row_set_selected(p->preset_combo, (guint)p->core->equalizer.preset);
    p->suppress = FALSE;

    sync_checks(p->band_checks, 3, band_count_index(p->core->equalizer.band_count));
    sync_checks(p->view_checks, 2, (int)p->core->equalizer.view);

    if (p->core->equalizer.view == AH_EQ_VIEW_CURVE)
        gtk_stack_set_visible_child_name(GTK_STACK(p->view_stack), "curve");
    else
        gtk_stack_set_visible_child_name(GTK_STACK(p->view_stack), "sliders");
}

static void on_preset_selected(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    EqPage *p = user_data;
    (void)pspec;
    if (p->suppress)
        return;

    guint id = adw_combo_row_get_selected(ADW_COMBO_ROW(obj));
    if (id >= AH_EQ_PRESET_COUNT)
        return;

    ah_core_set_eq_preset(p->core, (AhEqPresetId)id);
    rebuild_sliders(p);
    refresh_selection(p);
}

static void on_band_activated(AdwActionRow *row, gpointer user_data)
{
    EqPage *p = user_data;
    int count = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "band-count"));
    ah_core_set_band_count(p->core, (AhBandCount)count);
    rebuild_sliders(p);
    refresh_selection(p);
}

static void on_view_activated(AdwActionRow *row, gpointer user_data)
{
    EqPage *p = user_data;
    int view = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "view-id"));
    ah_core_set_eq_view(p->core, (AhEqViewMode)view);
    refresh_selection(p);
}

static AdwActionRow *make_choice_row(const char *title,
                                     const char *subtitle,
                                     const char *icon_name,
                                     GtkWidget **out_check)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle && subtitle[0])
        adw_action_row_set_subtitle(row, subtitle);
    adw_action_row_add_prefix(row, ah_icon_image(icon_name, 22));
    GtkWidget *check = ah_icon_image("object-select", 18);
    gtk_widget_set_opacity(check, 0.0);
    adw_action_row_add_suffix(row, check);
    *out_check = check;
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    return row;
}

void ah_gtk_equalizer_page_reload(GtkWidget *page)
{
    EqPage *p = g_object_get_data(G_OBJECT(page), "eq-page");
    if (!p)
        return;
    rebuild_sliders(p);
    refresh_selection(p);
}

GtkWidget *ah_gtk_equalizer_page_new(AhCore *core, AdwToastOverlay *toasts)
{
    EqPage *p = g_new0(EqPage, 1);
    p->core = core;
    (void)toasts;

    GtkWidget *page = adw_preferences_page_new();
    p->page = page;
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "Equalizer");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page),
                                       "multimedia-equalizer-symbolic");
    g_object_set_data_full(G_OBJECT(page), "eq-page", p, g_free);

    /* Presets — dropdown */
    AdwPreferencesGroup *presets = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(presets, "Presets");
    adw_preferences_group_set_description(
        presets, "Pick a starting curve. Editing any band switches to Custom and saves it.");

    GtkStringList *preset_model = gtk_string_list_new(NULL);
    const AhEqPresetDef *pdefs = ah_eq_preset_defs();
    for (int i = 0; i < AH_EQ_PRESET_COUNT; ++i)
        gtk_string_list_append(preset_model, pdefs[i].name);

    p->preset_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->preset_combo), "EQ Preset");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->preset_combo),
                                "Flat, Rock, Pop, Jazz, Custom, and more");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->preset_combo), ah_icon_image("preset", 22));
    adw_combo_row_set_model(p->preset_combo, G_LIST_MODEL(preset_model));
    adw_combo_row_set_selected(p->preset_combo, (guint)core->equalizer.preset);
    g_signal_connect(p->preset_combo, "notify::selected", G_CALLBACK(on_preset_selected), p);
    adw_preferences_group_add(presets, GTK_WIDGET(p->preset_combo));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), presets);

    /* Band configuration */
    AdwPreferencesGroup *bands = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(bands, "Band Configuration");
    adw_preferences_group_set_description(bands, "Number of graphic EQ bands sent to PipeWire.");

    const struct { AhBandCount count; const char *title; const char *sub; } band_opts[] = {
        { AH_BANDS_10, "10 bands", "Classic octave graphic EQ" },
        { AH_BANDS_15, "15 bands", "Finer control across the spectrum" },
        { AH_BANDS_20, "20 bands", "High-resolution third-octave style" },
    };
    for (int i = 0; i < 3; ++i) {
        AdwActionRow *row = make_choice_row(band_opts[i].title, band_opts[i].sub, "bands",
                                            &p->band_checks[i]);
        g_object_set_data(G_OBJECT(row), "band-count", GINT_TO_POINTER(band_opts[i].count));
        g_signal_connect(row, "activated", G_CALLBACK(on_band_activated), p);
        p->band_rows[i] = row;
        adw_preferences_group_add(bands, GTK_WIDGET(row));
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), bands);

    /* Equalizer view */
    AdwPreferencesGroup *view = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(view, "Equalizer View");

    AdwActionRow *curve_row = make_choice_row("Curve", "Frequency response plot", "view-curve",
                                              &p->view_checks[0]);
    g_object_set_data(G_OBJECT(curve_row), "view-id", GINT_TO_POINTER(AH_EQ_VIEW_CURVE));
    g_signal_connect(curve_row, "activated", G_CALLBACK(on_view_activated), p);
    p->view_rows[0] = curve_row;
    adw_preferences_group_add(view, GTK_WIDGET(curve_row));

    AdwActionRow *sliders_row = make_choice_row("Sliders", "Per-band vertical faders", "view-sliders",
                                                &p->view_checks[1]);
    g_object_set_data(G_OBJECT(sliders_row), "view-id", GINT_TO_POINTER(AH_EQ_VIEW_SLIDERS));
    g_signal_connect(sliders_row, "activated", G_CALLBACK(on_view_activated), p);
    p->view_rows[1] = sliders_row;
    adw_preferences_group_add(view, GTK_WIDGET(sliders_row));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), view);

    /* Live editor host */
    AdwPreferencesGroup *editor = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(editor, "Bands");
    adw_preferences_group_set_description(editor, "Drag to shape the PipeWire filter-chain EQ.");

    p->view_stack = gtk_stack_new();
    gtk_widget_set_margin_top(p->view_stack, 8);
    gtk_widget_set_margin_bottom(p->view_stack, 8);
    gtk_widget_set_margin_start(p->view_stack, 8);
    gtk_widget_set_margin_end(p->view_stack, 8);

    p->curve_area = gtk_drawing_area_new();
    gtk_widget_add_css_class(p->curve_area, "ah-eq-curve");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(p->curve_area), 320);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(p->curve_area), 180);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(p->curve_area), draw_curve, p, NULL);
    gtk_stack_add_named(GTK_STACK(p->view_stack), p->curve_area, "curve");

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    p->sliders_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(p->sliders_box, 4);
    gtk_widget_set_margin_bottom(p->sliders_box, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->sliders_box);
    gtk_stack_add_named(GTK_STACK(p->view_stack), scroll, "sliders");

    adw_preferences_group_add(editor, p->view_stack);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), editor);

    rebuild_sliders(p);
    refresh_selection(p);
    return page;
}
