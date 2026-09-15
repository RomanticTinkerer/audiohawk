#include "settings_page.h"

#include "icons.h"

#include <stdio.h>

typedef struct {
    AhCore *core;
    GtkWidget *page;
    gboolean suppress;

    AdwSwitchRow *bass_switch;
    AdwComboRow *bass_curve;
    AdwActionRow *bass_level_row;
    GtkWidget *bass_scale;

    AdwSwitchRow *mid_switch;
    AdwActionRow *mid_level_row;
    GtkWidget *mid_scale;

    AdwSwitchRow *treble_switch;
    AdwActionRow *treble_level_row;
    GtkWidget *treble_scale;

    AdwSwitchRow *leveler_switch;
    AdwActionRow *boost_row;
    GtkWidget *boost_scale;

    AdwSwitchRow *surround_switch;

    AdwSwitchRow *dialogue_switch;
    AdwActionRow *dialogue_strength_row;
    GtkWidget *dialogue_scale;

    AdwSwitchRow *startup_switch;
} SettingsPage;

static int scale_int(GtkWidget *scale)
{
    return (int)(gtk_range_get_value(GTK_RANGE(scale)) + 0.5);
}

static void set_scale(GtkWidget *scale, int value)
{
    gtk_range_set_value(GTK_RANGE(scale), (double)value);
}

static void push_effects(SettingsPage *p)
{
    if (!p || !p->core || p->suppress)
        return;

    AhEffectsState fx = p->core->effects;
    fx.bass_enhancer = adw_switch_row_get_active(p->bass_switch);
    fx.bass_curve = (AhBassCurve)adw_combo_row_get_selected(p->bass_curve);
    fx.bass_level = scale_int(p->bass_scale);
    fx.mid_enhancer = adw_switch_row_get_active(p->mid_switch);
    fx.mid_level = scale_int(p->mid_scale);
    fx.treble_enhancer = adw_switch_row_get_active(p->treble_switch);
    fx.treble_level = scale_int(p->treble_scale);
    fx.volume_leveler = adw_switch_row_get_active(p->leveler_switch);
    fx.volume_boost = scale_int(p->boost_scale);
    fx.surround_virtualizer = adw_switch_row_get_active(p->surround_switch);
    fx.dialogue_enhancer = adw_switch_row_get_active(p->dialogue_switch);
    fx.dialogue_strength = scale_int(p->dialogue_scale);
    ah_core_set_effects(p->core, &fx);
}

static void refresh_sensitivity(SettingsPage *p)
{
    gboolean bass = adw_switch_row_get_active(p->bass_switch);
    gboolean mid = adw_switch_row_get_active(p->mid_switch);
    gboolean treble = adw_switch_row_get_active(p->treble_switch);
    gboolean dialogue = adw_switch_row_get_active(p->dialogue_switch);

    gtk_widget_set_sensitive(GTK_WIDGET(p->bass_curve), bass);
    gtk_widget_set_sensitive(GTK_WIDGET(p->bass_level_row), bass);
    gtk_widget_set_sensitive(GTK_WIDGET(p->mid_level_row), mid);
    gtk_widget_set_sensitive(GTK_WIDGET(p->treble_level_row), treble);
    gtk_widget_set_sensitive(GTK_WIDGET(p->dialogue_strength_row), dialogue);
}

static void on_switch_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    SettingsPage *p = user_data;
    (void)obj;
    (void)pspec;
    if (p->suppress)
        return;
    refresh_sensitivity(p);
    push_effects(p);
}

static void on_curve_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    SettingsPage *p = user_data;
    (void)obj;
    (void)pspec;
    if (p->suppress)
        return;
    push_effects(p);
}

static void on_scale_changed(GtkRange *range, gpointer user_data)
{
    SettingsPage *p = user_data;
    if (p->suppress)
        return;

    /* Snap percent sliders to 5%; dialogue slider already steps by 1. */
    if (GTK_WIDGET(range) == p->bass_scale ||
        GTK_WIDGET(range) == p->mid_scale ||
        GTK_WIDGET(range) == p->treble_scale ||
        GTK_WIDGET(range) == p->boost_scale) {
        int v = scale_int(GTK_WIDGET(range));
        int lo = (GTK_WIDGET(range) == p->boost_scale) ? 100 : 0;
        int hi = (GTK_WIDGET(range) == p->boost_scale) ? 200 : 100;
        int snapped = lo + (((v - lo) + 2) / 5) * 5;
        if (snapped > hi)
            snapped = hi;
        if (snapped < lo)
            snapped = lo;
        if (snapped != v) {
            p->suppress = TRUE;
            set_scale(GTK_WIDGET(range), snapped);
            p->suppress = FALSE;
        }
    }
    push_effects(p);
}

static void on_startup_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    SettingsPage *p = user_data;
    (void)pspec;
    if (p->suppress)
        return;

    gboolean on = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
    if (ah_core_set_start_on_startup(p->core, on) != 0) {
        p->suppress = TRUE;
        adw_switch_row_set_active(ADW_SWITCH_ROW(obj), !on);
        p->suppress = FALSE;
    }
}

static GtkWidget *make_boost_scale(int value)
{
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 100, 200, 5);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(scale), 0);
    gtk_widget_set_size_request(scale, 160, -1);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
    set_scale(scale, value);
    return scale;
}

static GtkWidget *make_percent_scale(int value)
{
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 5);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(scale), 0);
    gtk_widget_set_size_request(scale, 160, -1);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
    set_scale(scale, value);
    return scale;
}

static char *format_percent(GtkScale *scale, double value, gpointer user_data)
{
    (void)scale;
    (void)user_data;
    return g_strdup_printf("%.0f%%", value);
}

static char *format_strength(GtkScale *scale, double value, gpointer user_data)
{
    (void)scale;
    (void)user_data;
    return g_strdup_printf("%.0f", value);
}

static GtkWidget *make_strength_scale(int value)
{
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 10, 1);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(scale), 0);
    gtk_widget_set_size_request(scale, 160, -1);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
    set_scale(scale, value);
    return scale;
}

static AdwActionRow *make_slider_row(const char *title,
                                     const char *subtitle,
                                     const char *icon,
                                     GtkWidget *scale)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle && subtitle[0])
        adw_action_row_set_subtitle(row, subtitle);
    adw_action_row_add_prefix(row, ah_icon_image(icon, 22));
    adw_action_row_add_suffix(row, scale);
    adw_action_row_set_activatable_widget(row, NULL);
    return row;
}

GtkWidget *ah_gtk_settings_page_new(AhCore *core, AdwToastOverlay *toasts)
{
    (void)toasts;

    SettingsPage *p = g_new0(SettingsPage, 1);
    p->core = core;

    GtkWidget *page = adw_preferences_page_new();
    p->page = page;
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "Settings");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page),
                                       "emblem-system-symbolic");
    g_object_set_data_full(G_OBJECT(page), "settings-page", p, g_free);

    const AhEffectsState *fx = &core->effects;

    /* ── Audio Tuning ─────────────────────────────────────────── */
    AdwPreferencesGroup *tuning = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(tuning, "Audio Tuning");
    adw_preferences_group_set_description(
        tuning, "Shape bass, mids, and treble on top of the equalizer curve.");

    p->bass_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->bass_switch), "Bass Enhancer");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->bass_switch),
                                "Add weight and harmonic richness to the low end");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->bass_switch),
                              ah_icon_image("effect-bass", 22));
    adw_preferences_group_add(tuning, GTK_WIDGET(p->bass_switch));

    p->bass_curve = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->bass_curve), "Bass Curve");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->bass_curve),
                                "Choose how the bass lift is shaped");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->bass_curve),
                              ah_icon_image("bass-curve", 22));
    GtkStringList *curves = gtk_string_list_new(NULL);
    const AhBassCurveDef *cdef = ah_bass_curve_defs();
    for (int i = 0; i < AH_BASS_CURVE_COUNT; ++i)
        gtk_string_list_append(curves, cdef[i].name);
    adw_combo_row_set_model(p->bass_curve, G_LIST_MODEL(curves));
    adw_preferences_group_add(tuning, GTK_WIDGET(p->bass_curve));

    p->bass_scale = make_percent_scale(fx->bass_level);
    gtk_scale_set_format_value_func(GTK_SCALE(p->bass_scale), format_percent, NULL, NULL);
    p->bass_level_row = make_slider_row("Bass Level",
                                        "0–100% in 5% steps",
                                        "effect-bass",
                                        p->bass_scale);
    adw_preferences_group_add(tuning, GTK_WIDGET(p->bass_level_row));

    p->mid_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->mid_switch), "Mid Enhancer");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->mid_switch),
                                "Bring forward body and instrument presence");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->mid_switch),
                              ah_icon_image("effect-mid", 22));
    adw_preferences_group_add(tuning, GTK_WIDGET(p->mid_switch));

    p->mid_scale = make_percent_scale(fx->mid_level);
    gtk_scale_set_format_value_func(GTK_SCALE(p->mid_scale), format_percent, NULL, NULL);
    p->mid_level_row = make_slider_row("Mid Level",
                                       "0–100% in 5% steps",
                                       "effect-mid",
                                       p->mid_scale);
    adw_preferences_group_add(tuning, GTK_WIDGET(p->mid_level_row));

    p->treble_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->treble_switch), "Treble Enhancer");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->treble_switch),
                                "Add air and sparkle to the top end");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->treble_switch),
                              ah_icon_image("effect-treble", 22));
    adw_preferences_group_add(tuning, GTK_WIDGET(p->treble_switch));

    p->treble_scale = make_percent_scale(fx->treble_level);
    gtk_scale_set_format_value_func(GTK_SCALE(p->treble_scale), format_percent, NULL, NULL);
    p->treble_level_row = make_slider_row("Treble Level",
                                          "0–100% in 5% steps",
                                          "effect-treble",
                                          p->treble_scale);
    adw_preferences_group_add(tuning, GTK_WIDGET(p->treble_level_row));

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), tuning);

    /* ── Volume ───────────────────────────────────────────────── */
    AdwPreferencesGroup *volume = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(volume, "Volume");
    adw_preferences_group_set_description(
        volume, "Leveling and optional digital gain on top of system volume.");

    p->leveler_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->leveler_switch), "Volume Leveler");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(p->leveler_switch),
        "Balances volume for a consistent listening experience");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->leveler_switch),
                              ah_icon_image("effect-leveler", 22));
    adw_preferences_group_add(volume, GTK_WIDGET(p->leveler_switch));

    p->boost_scale = make_boost_scale(fx->volume_boost);
    gtk_scale_set_format_value_func(GTK_SCALE(p->boost_scale), format_percent, NULL, NULL);
    p->boost_row = make_slider_row("Volume Boost",
                                   "100% = unity · up to 200% like VLC",
                                   "volume-boost",
                                   p->boost_scale);
    adw_preferences_group_add(volume, GTK_WIDGET(p->boost_row));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), volume);

    /* ── Surround Virtualizer ─────────────────────────────────── */
    AdwPreferencesGroup *surround = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(surround, "Surround Virtualizer");

    p->surround_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->surround_switch),
                                  "Speaker Surround Virtualizer");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(p->surround_switch),
        "Widens the stereo image and distributes sound across the two channels");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->surround_switch),
                              ah_icon_image("effect-surround", 22));
    adw_preferences_group_add(surround, GTK_WIDGET(p->surround_switch));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), surround);

    /* ── Dialogue Enhancement ─────────────────────────────────── */
    AdwPreferencesGroup *dialogue = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(dialogue, "Dialogue Enhancement");

    p->dialogue_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->dialogue_switch),
                                  "Dialogue Enhancer");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->dialogue_switch),
                                "Clarify speech and vocal presence");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->dialogue_switch),
                              ah_icon_image("effect-dialogue", 22));
    adw_preferences_group_add(dialogue, GTK_WIDGET(p->dialogue_switch));

    p->dialogue_scale = make_strength_scale(fx->dialogue_strength);
    gtk_scale_set_format_value_func(GTK_SCALE(p->dialogue_scale),
                                    format_strength, NULL, NULL);
    p->dialogue_strength_row = make_slider_row("Dialogue Enhancement Strength",
                                               "0–10 in steps of 1",
                                               "effect-dialogue",
                                               p->dialogue_scale);
    adw_preferences_group_add(dialogue, GTK_WIDGET(p->dialogue_strength_row));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), dialogue);

    /* ── Startup ──────────────────────────────────────────────── */
    AdwPreferencesGroup *startup = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(startup, "Startup");
    adw_preferences_group_set_description(
        startup,
        "Launch AudioHawk in the background when you log in. "
        "Equalizer state is restored from your last session.");

    p->startup_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->startup_switch),
                                  "Start on system startup");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(p->startup_switch),
        "Adds AudioHawk to your session autostart (XDG)");
    adw_action_row_add_prefix(ADW_ACTION_ROW(p->startup_switch),
                              gtk_image_new_from_icon_name(
                                  "preferences-system-startup-symbolic"));
    adw_preferences_group_add(startup, GTK_WIDGET(p->startup_switch));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), startup);

    /* Initial values */
    p->suppress = TRUE;
    adw_switch_row_set_active(p->bass_switch, fx->bass_enhancer);
    adw_combo_row_set_selected(p->bass_curve, (guint)fx->bass_curve);
    set_scale(p->bass_scale, fx->bass_level);
    adw_switch_row_set_active(p->mid_switch, fx->mid_enhancer);
    set_scale(p->mid_scale, fx->mid_level);
    adw_switch_row_set_active(p->treble_switch, fx->treble_enhancer);
    set_scale(p->treble_scale, fx->treble_level);
    adw_switch_row_set_active(p->leveler_switch, fx->volume_leveler);
    set_scale(p->boost_scale, fx->volume_boost);
    adw_switch_row_set_active(p->surround_switch, fx->surround_virtualizer);
    adw_switch_row_set_active(p->dialogue_switch, fx->dialogue_enhancer);
    set_scale(p->dialogue_scale, fx->dialogue_strength);
    adw_switch_row_set_active(p->startup_switch, core->settings.start_on_startup);
    refresh_sensitivity(p);
    p->suppress = FALSE;

    g_signal_connect(p->bass_switch, "notify::active", G_CALLBACK(on_switch_notify), p);
    g_signal_connect(p->mid_switch, "notify::active", G_CALLBACK(on_switch_notify), p);
    g_signal_connect(p->treble_switch, "notify::active", G_CALLBACK(on_switch_notify), p);
    g_signal_connect(p->leveler_switch, "notify::active", G_CALLBACK(on_switch_notify), p);
    g_signal_connect(p->surround_switch, "notify::active", G_CALLBACK(on_switch_notify), p);
    g_signal_connect(p->dialogue_switch, "notify::active", G_CALLBACK(on_switch_notify), p);
    g_signal_connect(p->startup_switch, "notify::active", G_CALLBACK(on_startup_notify), p);
    g_signal_connect(p->bass_curve, "notify::selected", G_CALLBACK(on_curve_notify), p);
    g_signal_connect(p->bass_scale, "value-changed", G_CALLBACK(on_scale_changed), p);
    g_signal_connect(p->mid_scale, "value-changed", G_CALLBACK(on_scale_changed), p);
    g_signal_connect(p->treble_scale, "value-changed", G_CALLBACK(on_scale_changed), p);
    g_signal_connect(p->boost_scale, "value-changed", G_CALLBACK(on_scale_changed), p);
    g_signal_connect(p->dialogue_scale, "value-changed", G_CALLBACK(on_scale_changed), p);

    return page;
}
