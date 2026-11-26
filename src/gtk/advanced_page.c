#include "advanced_page.h"
#include "icons.h"

#include "audiohawk/advanced.h"
#include "audiohawk/pipewire_eq.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const k_tab_titles[3] = { "Enhancement", "Fun", "Processing" };

typedef struct {
    AhCore *core;
    GtkWidget *root;
    GtkStackPage *tabs[3];
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
    AdwExpanderRow *row[AH_ADV_SLOTS]; /* its switch turns the effect on */
    GtkAdjustment *adj[AH_ADV_SLOTS][AH_ADV_PARAMS];
    guint meter_id;
    gboolean suppress;
} AdvPage;

/* Wheel over a control scrolls the page instead, so skimming the list can't change a value. */
static gboolean on_scale_scroll(GtkEventControllerScroll *ctl, double dx, double dy,
                                gpointer user_data)
{
    (void)dx;
    (void)user_data;
    GtkWidget *sw = gtk_widget_get_ancestor(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctl)), GTK_TYPE_SCROLLED_WINDOW);
    if (!sw)
        return FALSE;
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
    double step = 1.0;
    if (gtk_event_controller_scroll_get_unit(ctl) == GDK_SCROLL_UNIT_WHEEL)
        step = pow(gtk_adjustment_get_page_size(adj), 2.0 / 3.0);
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + dy * step);
    return TRUE;
}

static void scroll_page_instead(GtkWidget *w)
{
    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scale_scroll), NULL);
    gtk_widget_add_controller(w, scroll);
}

static void refresh_sensitivity(AdvPage *p)
{
    int active[3] = { 0, 0, 0 };
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        if (adw_expander_row_get_enable_expansion(p->row[i]))
            active[ah_advanced_slot_tab(i)]++;
    }
    gtk_widget_set_sensitive(GTK_WIDGET(p->app), adw_switch_row_get_active(p->per_app));

    for (int t = 0; t < 3; ++t) {
        char title[32];
        if (active[t])
            snprintf(title, sizeof(title), "%s (%d)", k_tab_titles[t], active[t]);
        else
            snprintf(title, sizeof(title), "%s", k_tab_titles[t]);
        gtk_stack_page_set_title(p->tabs[t], title);
    }
}

/* Hand-tuning an effect means the chain no longer matches the chosen preset. */
static void mark_custom(AdvPage *p)
{
    p->suppress = TRUE;
    adw_combo_row_set_selected(p->preset, 0);
    p->suppress = FALSE;
}

static void read_chain(AdvPage *p, AhAdvancedState *s)
{
    for (int slot = 0; slot < AH_ADV_SLOTS; ++slot) {
        s->on[slot] = adw_expander_row_get_enable_expansion(p->row[slot]);
        for (int i = 0; i < AH_ADV_PARAMS && p->adj[slot][i]; ++i)
            s->param[slot][i] = (float)gtk_adjustment_get_value(p->adj[slot][i]);
    }
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
    read_chain(p, &s);
    if (s.per_app) {
        GtkStringObject *obj = GTK_STRING_OBJECT(adw_combo_row_get_selected_item(p->app));
        if (obj)
            ah_advanced_select_app(&s, gtk_string_object_get_string(obj));
        read_chain(p, &s);
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
    for (int slot = 0; slot < AH_ADV_SLOTS; ++slot) {
        adw_expander_row_set_enable_expansion(p->row[slot], s->on[slot]);
        /* Switching an effect on opens it. Loading a whole chain shouldn't. */
        adw_expander_row_set_expanded(p->row[slot], FALSE);
        for (int i = 0; i < AH_ADV_PARAMS && p->adj[slot][i]; ++i)
            gtk_adjustment_set_value(p->adj[slot][i], s->param[slot][i]);
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
    (void)pspec;
    if (p->suppress)
        return;
    if (obj != G_OBJECT(p->ab) && obj != G_OBJECT(p->gain_match) && obj != G_OBJECT(p->per_app))
        mark_custom(p);
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

static void on_param(GtkAdjustment *adj, gpointer user_data)
{
    AdvPage *p = user_data;
    if (p->suppress)
        return;
    /* Dragging lands between steps, so snap to the control's own grid. */
    const AhAdvParam *d = g_object_get_data(G_OBJECT(adj), "ah-param");
    const double v = gtk_adjustment_get_value(adj);
    const double snapped =
        CLAMP(d->min + round((v - d->min) / d->step) * d->step, d->min, d->max);
    if (fabs(snapped - v) > 1e-6) {
        p->suppress = TRUE;
        gtk_adjustment_set_value(adj, snapped);
        p->suppress = FALSE;
    }
    mark_custom(p);
    push_state(p);
}

static void on_reset(GtkButton *button, gpointer user_data)
{
    AdvPage *p = user_data;
    const int slot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "ah-slot"));
    p->suppress = TRUE;
    for (int i = 0; i < AH_ADV_PARAMS && p->adj[slot][i]; ++i)
        gtk_adjustment_set_value(p->adj[slot][i], ah_advanced_param(slot, i)->def);
    p->suppress = FALSE;
    mark_custom(p);
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

static void format_db(char *buf, size_t n, float rms)
{
    if (rms < 0.0005f)
        snprintf(buf, n, "silent");
    else
        snprintf(buf, n, "%.1f dB", 20.0 * log10(rms));
}

static gboolean tick_meters(gpointer user_data)
{
    AdvPage *p = user_data;
    if (!p || !p->core || !gtk_widget_get_mapped(p->spec))
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
    char dry[16], wet[16];
    format_db(dry, sizeof(dry), m.dry_rms);
    format_db(wet, sizeof(wet), m.wet_rms);
    snprintf(buf, sizeof(buf), "%s / %s", dry, wet);
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

/* One control: name, slider, exact value, unit. Slider and number share one adjustment. */
static GtkWidget *make_param_row(AdvPage *p, int slot, int i)
{
    const AhAdvParam *d = ah_advanced_param(slot, i);
    GtkAdjustment *adj =
        gtk_adjustment_new(p->core->advanced.param[slot][i], d->min, d->max, d->step,
                           d->step * 10.0, 0.0);
    g_object_set_data(G_OBJECT(adj), "ah-param", (gpointer)d);
    p->adj[slot][i] = adj;

    GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_scale_add_mark(GTK_SCALE(scale), d->def, GTK_POS_BOTTOM, NULL); /* the default */
    gtk_widget_set_size_request(scale, 220, -1);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
    scroll_page_instead(scale);

    const guint digits = d->step >= 1.f ? 0 : d->step >= 0.1f ? 1 : 2;
    GtkWidget *spin = gtk_spin_button_new(adj, d->step, digits);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_editable_set_width_chars(GTK_EDITABLE(spin), 7);
    gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);
    scroll_page_instead(spin);

    GtkWidget *unit = gtk_label_new(d->unit);
    gtk_label_set_width_chars(GTK_LABEL(unit), 4);
    gtk_label_set_xalign(GTK_LABEL(unit), 0.f);
    gtk_widget_add_css_class(unit, "dim-label");

    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), d->label);
    adw_action_row_add_suffix(row, scale);
    adw_action_row_add_suffix(row, spin);
    adw_action_row_add_suffix(row, unit);
    g_signal_connect(adj, "value-changed", G_CALLBACK(on_param), p);
    return GTK_WIDGET(row);
}

static void add_slot_row(AdvPage *p, AdwPreferencesGroup *g, int slot)
{
    AdwExpanderRow *row = ADW_EXPANDER_ROW(adw_expander_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), ah_advanced_slot_title(slot));
    adw_expander_row_set_subtitle(row, ah_advanced_slot_blurb(slot));
    adw_expander_row_add_prefix(row, ah_icon_image(ah_advanced_slot_icon(slot), 22));
    adw_expander_row_set_show_enable_switch(row, TRUE);
    adw_expander_row_set_enable_expansion(row, p->core->advanced.on[slot]);
    adw_expander_row_set_expanded(row, FALSE);
    for (int i = 0; i < ah_advanced_param_count(slot); ++i)
        adw_expander_row_add_row(row, make_param_row(p, slot, i));

    GtkWidget *reset = gtk_button_new_from_icon_name("edit-undo-symbolic");
    gtk_widget_set_tooltip_text(reset, "Reset to defaults");
    gtk_widget_set_valign(reset, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(reset, "flat");
    g_object_set_data(G_OBJECT(reset), "ah-slot", GINT_TO_POINTER(slot));
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset), p);
    AdwActionRow *reset_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(reset_row), "Reset to defaults");
    adw_action_row_add_suffix(reset_row, reset);
    adw_action_row_set_activatable_widget(reset_row, reset);
    adw_expander_row_add_row(row, GTK_WIDGET(reset_row));

    adw_preferences_group_add(g, GTK_WIDGET(row));
    p->row[slot] = row;
    g_signal_connect(row, "notify::enable-expansion", G_CALLBACK(on_switch), p);
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
        "Centre", "Tone", "Dynamics", "Loudness", "Stereo", "Cleanup", "Check", NULL
    };
    static const char *fun_groups[] = {
        "Character", "Space", "Tone", NULL
    };
    static const char *process_groups[] = {
        "Equalizer", "Dynamics", "Cleanup", "Space", NULL
    };

    p->tabs[0] = gtk_stack_add_titled(
        GTK_STACK(stack), GTK_WIDGET(make_slot_page(p, 0, k_tab_titles[0], enhance_groups)),
        "enhance", k_tab_titles[0]);
    p->tabs[1] = gtk_stack_add_titled(
        GTK_STACK(stack), GTK_WIDGET(make_slot_page(p, 1, k_tab_titles[1], fun_groups)),
        "fun", k_tab_titles[1]);
    p->tabs[2] = gtk_stack_add_titled(
        GTK_STACK(stack), GTK_WIDGET(make_slot_page(p, 2, k_tab_titles[2], process_groups)),
        "process", k_tab_titles[2]);

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
    p->gain_lab = gtk_label_new("silent / silent");
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
