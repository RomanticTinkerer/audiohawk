#include "advanced_page.h"
#include "icons.h"

#include "audiohawk/advanced.h"
#include "audiohawk/pipewire_eq.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    AhCore *core;
    GtkWidget *root;
    GtkWidget *spec;
    GtkWidget *clip_lab;
    GtkWidget *cpu_lab;
    GtkWidget *corr_lab;
    GtkWidget *gain_lab;
    AdwSwitchRow *ab;
    AdwSwitchRow *gain_match;
    AdwSwitchRow *per_app;
    AdwComboRow *preset;
    AdwComboRow *order;
    AdwComboRow *app;
    AdwSwitchRow *sw[AH_ADV_SLOTS];
    GtkWidget *scale[AH_ADV_SLOTS];
    guint meter_id;
    gboolean suppress;
} AdvPage;

static int scale_int(GtkWidget *scale)
{
    return (int)(gtk_range_get_value(GTK_RANGE(scale)) + 0.5);
}

static void set_scale(GtkWidget *scale, int value)
{
    gtk_range_set_value(GTK_RANGE(scale), (double)value);
}

static char *format_percent(GtkScale *scale, double value, gpointer user_data)
{
    (void)scale;
    (void)user_data;
    return g_strdup_printf("%.0f%%", value);
}

static GtkWidget *make_percent_scale(int value)
{
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 5);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(scale), 0);
    gtk_widget_set_size_request(scale, 140, -1);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
    gtk_scale_set_format_value_func(GTK_SCALE(scale), format_percent, NULL, NULL);
    set_scale(scale, value);
    return scale;
}

static void refresh_sensitivity(AdvPage *p)
{
    for (int i = 0; i < AH_ADV_SLOTS; ++i)
        gtk_widget_set_sensitive(p->scale[i], adw_switch_row_get_active(p->sw[i]));
    gtk_widget_set_sensitive(GTK_WIDGET(p->app), adw_switch_row_get_active(p->per_app));
}

static void push_state(AdvPage *p)
{
    if (!p || !p->core || p->suppress)
        return;

    AhAdvancedState s = p->core->advanced;
    s.ab_bypass = adw_switch_row_get_active(p->ab);
    s.gain_match = adw_switch_row_get_active(p->gain_match);
    s.per_app = adw_switch_row_get_active(p->per_app);
    s.order = (int)adw_combo_row_get_selected(p->order);
    s.preset = (int)adw_combo_row_get_selected(p->preset);
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        s.on[i] = adw_switch_row_get_active(p->sw[i]);
        s.amount[i] = scale_int(p->scale[i]);
    }
    if (s.per_app) {
        GtkStringObject *obj = GTK_STRING_OBJECT(adw_combo_row_get_selected_item(p->app));
        if (obj)
            ah_advanced_select_app(&s, gtk_string_object_get_string(obj));
        for (int i = 0; i < AH_ADV_SLOTS; ++i) {
            s.on[i] = adw_switch_row_get_active(p->sw[i]);
            s.amount[i] = scale_int(p->scale[i]);
        }
        ah_advanced_store_app(&s);
    }
    ah_core_set_advanced(p->core, &s);
}

static void fill_from_core(AdvPage *p)
{
    const AhAdvancedState *s = &p->core->advanced;
    p->suppress = TRUE;
    adw_switch_row_set_active(p->ab, s->ab_bypass);
    adw_switch_row_set_active(p->gain_match, s->gain_match);
    adw_switch_row_set_active(p->per_app, s->per_app);
    adw_combo_row_set_selected(p->preset, (guint)s->preset);
    adw_combo_row_set_selected(p->order, (guint)s->order);
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        adw_switch_row_set_active(p->sw[i], s->on[i]);
        set_scale(p->scale[i], s->amount[i]);
    }
    guint app_sel = 0;
    for (int i = 0; i < s->n_apps; ++i) {
        if (strcmp(s->apps[i].name, s->app_name) == 0)
            app_sel = (guint)i;
    }
    if (s->n_apps > 0)
        adw_combo_row_set_selected(p->app, app_sel);
    refresh_sensitivity(p);
    p->suppress = FALSE;
}

static void on_switch(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    AdvPage *p = user_data;
    (void)obj;
    (void)pspec;
    if (p->suppress)
        return;
    refresh_sensitivity(p);
    push_state(p);
}

static void on_combo(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    AdvPage *p = user_data;
    (void)pspec;
    if (p->suppress)
        return;
    if (ADW_COMBO_ROW(obj) == p->preset) {
        int id = (int)adw_combo_row_get_selected(p->preset);
        if (id > 0) {
            AhAdvancedState s = p->core->advanced;
            ah_advanced_apply_preset(&s, id);
            p->core->advanced = s;
            fill_from_core(p);
        }
    }
    if (ADW_COMBO_ROW(obj) == p->app && adw_switch_row_get_active(p->per_app)) {
        GtkStringObject *item = GTK_STRING_OBJECT(adw_combo_row_get_selected_item(p->app));
        if (item) {
            AhAdvancedState s = p->core->advanced;
            ah_advanced_select_app(&s, gtk_string_object_get_string(item));
            p->core->advanced = s;
            fill_from_core(p);
        }
    }
    push_state(p);
}

static void on_scale(GtkRange *range, gpointer user_data)
{
    AdvPage *p = user_data;
    if (p->suppress)
        return;
    int v = scale_int(GTK_WIDGET(range));
    int snapped = ((v + 2) / 5) * 5;
    if (snapped > 100)
        snapped = 100;
    if (snapped < 0)
        snapped = 0;
    if (snapped != v) {
        p->suppress = TRUE;
        set_scale(GTK_WIDGET(range), snapped);
        p->suppress = FALSE;
    }
    push_state(p);
}

static void draw_spec(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer user_data)
{
    AdvPage *p = user_data;
    (void)area;
    AhAdvancedMeters m;
    memset(&m, 0, sizeof(m));
    if (p->core && p->core->eq)
        ah_pw_eq_advanced_meters(p->core->eq, &m);

    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    const int n = AH_ADV_SPEC;
    double gap = 2.0;
    double bw = (w - gap * (n + 1)) / (double)n;
    for (int i = 0; i < n; ++i) {
        double v = m.spec[i];
        if (v < 0)
            v = 0;
        if (v > 1)
            v = 1;
        double bh = v * (h - 8);
        cairo_set_source_rgba(cr, 0.35, 0.62, 0.95, 0.9);
        cairo_rectangle(cr, gap + i * (bw + gap), h - 4 - bh, bw, bh);
        cairo_fill(cr);
    }
}

static gboolean tick_meters(gpointer user_data)
{
    AdvPage *p = user_data;
    if (!p || !p->core)
        return G_SOURCE_CONTINUE;

    AhAdvancedMeters m;
    memset(&m, 0, sizeof(m));
    if (p->core->eq)
        ah_pw_eq_advanced_meters(p->core->eq, &m);

    gtk_label_set_text(GTK_LABEL(p->clip_lab), m.clip ? "CLIP" : "Peak OK");
    if (m.clip)
        gtk_widget_add_css_class(p->clip_lab, "ah-clip-hot");
    else
        gtk_widget_remove_css_class(p->clip_lab, "ah-clip-hot");

    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f%%", m.cpu * 100.f);
    gtk_label_set_text(GTK_LABEL(p->cpu_lab), buf);
    snprintf(buf, sizeof(buf), "%+0.2f", m.corr);
    gtk_label_set_text(GTK_LABEL(p->corr_lab), buf);
    snprintf(buf, sizeof(buf), "%.3f / %.3f", m.dry_rms, m.wet_rms);
    gtk_label_set_text(GTK_LABEL(p->gain_lab), buf);

    if (p->spec)
        gtk_widget_queue_draw(p->spec);
    return G_SOURCE_CONTINUE;
}

static void on_destroy(GtkWidget *w, gpointer user_data)
{
    AdvPage *p = user_data;
    (void)w;
    if (p->meter_id) {
        g_source_remove(p->meter_id);
        p->meter_id = 0;
    }
}

static void add_slot_row(AdvPage *p, AdwPreferencesGroup *g, int i)
{
    AdwSwitchRow *sw = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sw), ah_advanced_slot_title(i));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(sw), ah_advanced_slot_blurb(i));
    adw_action_row_add_prefix(ADW_ACTION_ROW(sw),
                              ah_icon_image(ah_advanced_slot_icon(i), 22));
    GtkWidget *scale = make_percent_scale(p->core->advanced.amount[i]);
    adw_action_row_add_suffix(ADW_ACTION_ROW(sw), scale);
    adw_preferences_group_add(g, GTK_WIDGET(sw));
    p->sw[i] = sw;
    p->scale[i] = scale;
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_switch), p);
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale), p);
}

static AdwPreferencesPage *make_slot_page(AdvPage *p, int tab, const char *title,
                                         const char *const *order)
{

    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, title);

    for (int g = 0; order[g]; ++g) {
        gboolean any = FALSE;
        for (int i = 0; i < AH_ADV_SLOTS; ++i) {
            if (ah_advanced_slot_tab(i) == tab &&
                strcmp(ah_advanced_slot_group(i), order[g]) == 0) {
                any = TRUE;
                break;
            }
        }
        if (!any)
            continue;

        AdwPreferencesGroup *grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        adw_preferences_group_set_title(grp, order[g]);
        for (int i = 0; i < AH_ADV_SLOTS; ++i) {
            if (ah_advanced_slot_tab(i) != tab)
                continue;
            if (strcmp(ah_advanced_slot_group(i), order[g]) != 0)
                continue;
            add_slot_row(p, grp, i);
        }
        adw_preferences_page_add(page, grp);
    }
    return page;
}

GtkWidget *ah_gtk_advanced_page_new(AhCore *core, AdwToastOverlay *toasts)
{
    (void)toasts;
    AdvPage *p = g_new0(AdvPage, 1);
    p->core = core;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    p->root = box;
    g_object_set_data_full(G_OBJECT(box), "adv-page", p, g_free);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(switcher, 8);
    gtk_widget_set_margin_bottom(switcher, 4);
    gtk_box_append(GTK_BOX(box), switcher);

    static const char *enhance_groups[] = {
        "Tone", "Dynamics", "Loudness", "Stereo", "Cleanup", "Check", NULL
    };
    static const char *fun_groups[] = {
        "Character", "Space", "Tone", NULL
    };
    static const char *process_groups[] = {
        "Equalizer", "Dynamics", "Cleanup", "Space", NULL
    };

    gtk_stack_add_titled(GTK_STACK(stack),
                         GTK_WIDGET(make_slot_page(p, 0, "Enhancement", enhance_groups)),
                         "enhance", "Enhancement");
    gtk_stack_add_titled(GTK_STACK(stack),
                         GTK_WIDGET(make_slot_page(p, 1, "Fun", fun_groups)),
                         "fun", "Fun");
    gtk_stack_add_titled(GTK_STACK(stack),
                         GTK_WIDGET(make_slot_page(p, 2, "Processing", process_groups)),
                         "process", "Processing");

    AdwPreferencesPage *mon = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(mon, "Monitor");

    AdwPreferencesGroup *compare = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(compare, "Comparison");
    adw_preferences_group_set_description(
        compare, "A/B bypasses the whole Advanced chain. Equalizer must stay on.");

    p->ab = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->ab), "A/B Audio Comparison");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->ab),
                                "Bypass every Advanced effect and hear the dry path");
    adw_preferences_group_add(compare, GTK_WIDGET(p->ab));

    p->gain_match = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->gain_match), "Automatic Gain Matching");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->gain_match),
                                "Match wet loudness to the dry signal");
    adw_preferences_group_add(compare, GTK_WIDGET(p->gain_match));
    adw_preferences_page_add(mon, compare);

    AdwPreferencesGroup *meters = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(meters, "Meters");

    AdwActionRow *clip_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(clip_row), "Clipping Indicator");
    p->clip_lab = gtk_label_new("Peak OK");
    gtk_widget_add_css_class(p->clip_lab, "ah-clip");
    adw_action_row_add_suffix(clip_row, p->clip_lab);
    adw_preferences_group_add(meters, GTK_WIDGET(clip_row));

    AdwActionRow *cpu_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(cpu_row), "DSP CPU Usage");
    p->cpu_lab = gtk_label_new("0.0%");
    adw_action_row_add_suffix(cpu_row, p->cpu_lab);
    adw_preferences_group_add(meters, GTK_WIDGET(cpu_row));

    AdwActionRow *corr_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(corr_row), "Mono Compatibility");
    p->corr_lab = gtk_label_new("+1.00");
    adw_action_row_add_suffix(corr_row, p->corr_lab);
    adw_preferences_group_add(meters, GTK_WIDGET(corr_row));

    AdwActionRow *gain_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(gain_row), "Dry / Wet Level");
    p->gain_lab = gtk_label_new("0 / 0");
    adw_action_row_add_suffix(gain_row, p->gain_lab);
    adw_preferences_group_add(meters, GTK_WIDGET(gain_row));
    adw_preferences_page_add(mon, meters);

    AdwPreferencesGroup *spec_g = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(spec_g, "Spectrum");
    p->spec = gtk_drawing_area_new();
    gtk_widget_add_css_class(p->spec, "ah-spectrum");
    gtk_widget_set_size_request(p->spec, -1, 110);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(p->spec), draw_spec, p, NULL);
    adw_preferences_group_add(spec_g, p->spec);
    adw_preferences_page_add(mon, spec_g);

    AdwPreferencesGroup *chain = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(chain, "Effect chains");

    p->preset = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->preset), "Chain Preset");
    GtkStringList *presets = gtk_string_list_new(NULL);
    for (int i = 0; i < AH_ADV_PRESETS; ++i)
        gtk_string_list_append(presets, ah_advanced_preset_name(i));
    adw_combo_row_set_model(p->preset, G_LIST_MODEL(presets));
    adw_preferences_group_add(chain, GTK_WIDGET(p->preset));

    p->order = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->order), "Processing Order");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->order),
                                "Cleanup first, then these blocks, then limiter");
    GtkStringList *orders = gtk_string_list_new(NULL);
    for (int i = 0; i < AH_ADV_ORDERS; ++i)
        gtk_string_list_append(orders, ah_advanced_order_name(i));
    adw_combo_row_set_model(p->order, G_LIST_MODEL(orders));
    adw_preferences_group_add(chain, GTK_WIDGET(p->order));
    adw_preferences_page_add(mon, chain);

    AdwPreferencesGroup *apps_g = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(apps_g, "Per application");

    p->per_app = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->per_app), "Per-Application Effect Chains");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->per_app),
                                "Keep a separate Advanced chain per named app");
    adw_preferences_group_add(apps_g, GTK_WIDGET(p->per_app));

    p->app = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->app), "Active app chain");
    GtkStringList *apps = gtk_string_list_new(NULL);
    for (int i = 0; i < core->advanced.n_apps; ++i)
        gtk_string_list_append(apps, core->advanced.apps[i].name);
    if (core->advanced.n_apps == 0)
        gtk_string_list_append(apps, "default");
    adw_combo_row_set_model(p->app, G_LIST_MODEL(apps));
    adw_preferences_group_add(apps_g, GTK_WIDGET(p->app));
    adw_preferences_page_add(mon, apps_g);

    gtk_stack_add_titled(GTK_STACK(stack), GTK_WIDGET(mon), "monitor", "Monitor");
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_box_append(GTK_BOX(box), stack);

    g_signal_connect(p->ab, "notify::active", G_CALLBACK(on_switch), p);
    g_signal_connect(p->gain_match, "notify::active", G_CALLBACK(on_switch), p);
    g_signal_connect(p->per_app, "notify::active", G_CALLBACK(on_switch), p);
    g_signal_connect(p->preset, "notify::selected", G_CALLBACK(on_combo), p);
    g_signal_connect(p->order, "notify::selected", G_CALLBACK(on_combo), p);
    g_signal_connect(p->app, "notify::selected", G_CALLBACK(on_combo), p);

    fill_from_core(p);
    p->meter_id = g_timeout_add(100, tick_meters, p);
    g_signal_connect(box, "destroy", G_CALLBACK(on_destroy), p);
    return box;
}
