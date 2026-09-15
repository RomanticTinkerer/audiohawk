#include "dashboard.h"
#include "icons.h"

#include <string.h>

typedef struct {
    AhCore *core;
    AdwToastOverlay *toasts;
    GtkWidget *page;
    AdwBanner *banner;
    AdwSwitchRow *eq_switch;
    AdwSwitchRow *auto_switch;
    AdwSwitchRow *device_memory;
    AdwPreferencesGroup *profile_group;
    AdwPreferencesGroup *intel_group;
    AdwActionRow *profile_rows[AH_PROFILE_COUNT];
    GtkWidget *profile_checks[AH_PROFILE_COUNT];
    AdwActionRow *intel_rows[AH_INTEL_COUNT];
    GtkWidget *intel_checks[AH_INTEL_COUNT];
    gboolean suppress;
} Dash;

static const char *profile_icon(AhProfileId id)
{
    switch (id) {
    case AH_PROFILE_MUSIC:  return "music";
    case AH_PROFILE_MOVIE:  return "movie";
    case AH_PROFILE_GAME:   return "game";
    case AH_PROFILE_WORK:   return "work";
    case AH_PROFILE_CASUAL: return "casual";
    case AH_PROFILE_MOOD:   return "mood";
    }
    return "music";
}

static const char *intel_icon(AhIntelMode id)
{
    switch (id) {
    case AH_INTEL_OFF:      return "intel-off";
    case AH_INTEL_DETAILED: return "intel-detailed";
    case AH_INTEL_WARM:     return "intel-warm";
    case AH_INTEL_BALANCED: return "intel-balanced";
    }
    return "intel-off";
}

static void toast(Dash *d, const char *msg)
{
    if (!d->toasts || !msg)
        return;
    adw_toast_overlay_add_toast(d->toasts, adw_toast_new(msg));
}

static void sync_checks(GtkWidget **checks, int count, int active)
{
    for (int i = 0; i < count; ++i)
        gtk_widget_set_opacity(checks[i], i == active ? 1.0 : 0.0);
}

static void refresh_group_blurbs(Dash *d)
{
    const AhProfileDef *p = ah_profile_by_id(d->core->settings.profile);
    const AhIntelDef *i = ah_intel_by_id(d->core->settings.intel);
    adw_preferences_group_set_description(d->profile_group, p->blurb);
    adw_preferences_group_set_description(d->intel_group, i->blurb);
}

static void refresh_banner(Dash *d)
{
    if (d->core->settings.eq_enabled) {
        const AhProfileDef *p = ah_profile_by_id(d->core->settings.profile);
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Equalizer on — processing output streams (%s · %s)",
                 p->name,
                 ah_intel_by_id(d->core->settings.intel)->name);
        adw_banner_set_title(d->banner, buf);
        adw_banner_set_revealed(d->banner, TRUE);
    } else {
        adw_banner_set_title(d->banner, "Equalizer is off");
        adw_banner_set_revealed(d->banner, FALSE);
    }
}

static void refresh_selection(Dash *d)
{
    sync_checks(d->profile_checks, AH_PROFILE_COUNT, (int)d->core->settings.profile);
    sync_checks(d->intel_checks, AH_INTEL_COUNT, (int)d->core->settings.intel);
    refresh_group_blurbs(d);
    refresh_banner(d);
}

static void on_eq_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    Dash *d = user_data;
    (void)pspec;
    if (d->suppress)
        return;

    gboolean on = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
    if (ah_core_set_eq_enabled(d->core, on) != 0) {
        d->suppress = TRUE;
        adw_switch_row_set_active(ADW_SWITCH_ROW(obj), FALSE);
        d->suppress = FALSE;
        const char *err = ah_pw_eq_last_error(d->core->eq);
        toast(d, err && err[0] ? err : "Could not enable PipeWire equalizer");
        return;
    }
    refresh_banner(d);
}

static void on_auto_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    Dash *d = user_data;
    (void)pspec;
    if (d->suppress)
        return;
    ah_core_set_auto_switch(d->core, adw_switch_row_get_active(ADW_SWITCH_ROW(obj)));
}

static void on_device_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    Dash *d = user_data;
    (void)pspec;
    if (d->suppress)
        return;
    ah_core_set_per_device_memory(d->core, adw_switch_row_get_active(ADW_SWITCH_ROW(obj)));
}

static void on_profile_activated(AdwActionRow *row, gpointer user_data)
{
    Dash *d = user_data;
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "profile-id"));
    ah_core_set_profile(d->core, (AhProfileId)id);
    refresh_selection(d);
}

static void on_intel_activated(AdwActionRow *row, gpointer user_data)
{
    Dash *d = user_data;
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "intel-id"));
    ah_core_set_intel(d->core, (AhIntelMode)id);
    refresh_selection(d);
}

static void on_manage_chosen(GObject *src, GAsyncResult *res, gpointer user_data)
{
    Dash *d = user_data;
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(src);
    const char *response = adw_alert_dialog_choose_finish(dlg, res);

    if (g_strcmp0(response, "seed") == 0) {
        ah_app_profiles_upsert(&d->core->app_profiles, "firefox", AH_PROFILE_MOVIE, true);
        ah_app_profiles_upsert(&d->core->app_profiles, "spotify", AH_PROFILE_MUSIC, true);
        ah_app_profiles_upsert(&d->core->app_profiles, "steam", AH_PROFILE_GAME, true);
    }
}

static void on_manage_activated(AdwActionRow *row, gpointer user_data)
{
    Dash *d = user_data;
    (void)row;

    AdwDialog *dialog = adw_alert_dialog_new(
        "Manage App Profiles",
        "Assign listening profiles to individual applications. "
        "Mappings are stored in ~/.config/audiohawk/app_profiles.conf.\n\n"
        "A full editor page will follow — you can seed a few examples now.");

    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),
                                   "close", "_Close",
                                   "seed", "_Seed Examples",
                                   NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog),
                                             "seed",
                                             ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "close");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "close");

    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog),
                            d->page,
                            NULL,
                            on_manage_chosen,
                            d);
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

GtkWidget *ah_gtk_dashboard_new(AhCore *core, AdwToastOverlay *toasts)
{
    Dash *d = g_new0(Dash, 1);
    d->core = core;
    d->toasts = toasts;

    GtkWidget *page = adw_preferences_page_new();
    d->page = page;
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "Dashboard");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page),
                                       "multimedia-volume-control-symbolic");
    g_object_set_data_full(G_OBJECT(page), "dash", d, g_free);

    d->banner = ADW_BANNER(adw_banner_new(""));
    adw_preferences_page_set_banner(ADW_PREFERENCES_PAGE(page), d->banner);

    /* ── EQ Settings ─────────────────────────────────────────── */
    AdwPreferencesGroup *eq_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(eq_group, "EQ Settings");
    adw_preferences_group_set_description(
        eq_group, "Enable the AudioHawk PipeWire equalizer sink.");

    d->eq_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(d->eq_switch), "Equalizer");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(d->eq_switch),
                                "Apply the selected profile curve to system audio");
    adw_action_row_add_prefix(ADW_ACTION_ROW(d->eq_switch), ah_icon_image("eq", 22));
    adw_preferences_group_add(eq_group, GTK_WIDGET(d->eq_switch));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), eq_group);

    /* ── Profiles (one row each) ─────────────────────────────── */
    d->profile_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(d->profile_group, "Profiles");

    const AhProfileDef *pdefs = ah_profile_defs();
    for (int i = 0; i < AH_PROFILE_COUNT; ++i) {
        AdwActionRow *row = make_choice_row(pdefs[i].name,
                                            pdefs[i].blurb,
                                            profile_icon((AhProfileId)i),
                                            &d->profile_checks[i]);
        g_object_set_data(G_OBJECT(row), "profile-id", GINT_TO_POINTER(i));
        g_signal_connect(row, "activated", G_CALLBACK(on_profile_activated), d);
        d->profile_rows[i] = row;
        adw_preferences_group_add(d->profile_group, GTK_WIDGET(row));
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), d->profile_group);

    /* ── Intelligent Equalizer (one row each) ────────────────── */
    d->intel_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(d->intel_group, "Intelligent Equalizer");

    const AhIntelDef *idefs = ah_intel_defs();
    for (int i = 0; i < AH_INTEL_COUNT; ++i) {
        AdwActionRow *row = make_choice_row(idefs[i].name,
                                            idefs[i].blurb,
                                            intel_icon((AhIntelMode)i),
                                            &d->intel_checks[i]);
        g_object_set_data(G_OBJECT(row), "intel-id", GINT_TO_POINTER(i));
        g_signal_connect(row, "activated", G_CALLBACK(on_intel_activated), d);
        d->intel_rows[i] = row;
        adw_preferences_group_add(d->intel_group, GTK_WIDGET(row));
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), d->intel_group);

    /* ── Per App Audio Profiles ──────────────────────────────── */
    AdwPreferencesGroup *app_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(app_group, "Per App Audio Profiles");
    adw_preferences_group_set_description(
        app_group,
        "Remember and switch listening profiles per application or device.");

    d->auto_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(d->auto_switch), "Auto-switch profiles");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(d->auto_switch),
                                "Follow the focused app and apply its saved profile");
    adw_action_row_add_prefix(ADW_ACTION_ROW(d->auto_switch),
                              ah_icon_image("auto-switch", 22));
    adw_preferences_group_add(app_group, GTK_WIDGET(d->auto_switch));

    d->device_memory = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(d->device_memory),
                                  "Per-device Audio Memory");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(d->device_memory),
                                "Remember EQ and profile choices for each output device");
    adw_action_row_add_prefix(ADW_ACTION_ROW(d->device_memory),
                              ah_icon_image("device-memory", 22));
    adw_preferences_group_add(app_group, GTK_WIDGET(d->device_memory));

    AdwActionRow *manage = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(manage), "Manage App Profiles");
    adw_action_row_set_subtitle(manage, "Assign profiles to individual applications");
    adw_action_row_add_prefix(manage, ah_icon_image("manage-apps", 22));
    GtkWidget *go = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_set_valign(go, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(manage, go);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(manage), TRUE);
    g_signal_connect(manage, "activated", G_CALLBACK(on_manage_activated), d);
    adw_preferences_group_add(app_group, GTK_WIDGET(manage));

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), app_group);

    /* Initial state */
    d->suppress = TRUE;
    adw_switch_row_set_active(d->eq_switch, core->settings.eq_enabled);
    adw_switch_row_set_active(d->auto_switch, core->settings.auto_switch_profiles);
    adw_switch_row_set_active(d->device_memory, core->settings.per_device_memory);
    d->suppress = FALSE;

    g_signal_connect(d->eq_switch, "notify::active", G_CALLBACK(on_eq_notify), d);
    g_signal_connect(d->auto_switch, "notify::active", G_CALLBACK(on_auto_notify), d);
    g_signal_connect(d->device_memory, "notify::active", G_CALLBACK(on_device_notify), d);

    refresh_selection(d);
    return page;
}
