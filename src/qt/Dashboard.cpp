#include "Dashboard.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

Dashboard::Dashboard(AhCore *core, QWidget *parent)
    : QWidget(parent)
    , m_core(core)
{
    setObjectName(QStringLiteral("ahRoot"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 28);
    root->setSpacing(0);

    auto *brand = new QLabel(QStringLiteral("AudioHawk"));
    brand->setObjectName(QStringLiteral("ahBrand"));
    root->addWidget(brand);

    auto *sub = new QLabel(QStringLiteral("PIPEWIRE EQUALIZER"));
    sub->setObjectName(QStringLiteral("ahBrandSub"));
    root->addWidget(sub);

    m_rail = new QFrame;
    m_rail->setObjectName(QStringLiteral("ahSignalRail"));
    m_rail->setProperty("armed", false);
    root->addSpacing(14);
    root->addWidget(m_rail);
    root->addSpacing(22);

    auto makePanel = [](QVBoxLayout *into) {
        auto *panel = new QFrame;
        panel->setProperty("class", QStringLiteral("ahPanel"));
        panel->setStyleSheet(
            "QFrame { background:#1A1F27; border:1px solid #2C3440; border-radius:10px; }");
        auto *lay = new QVBoxLayout(panel);
        lay->setContentsMargins(16, 16, 16, 16);
        lay->setSpacing(10);
        into->addWidget(panel);
        into->addSpacing(16);
        return lay;
    };

    /* EQ master */
    {
        auto *lay = makePanel(root);
        auto *sec = new QLabel(QStringLiteral("EQ SETTINGS"));
        sec->setProperty("class", QStringLiteral("ahSection"));
        sec->setStyleSheet("color:#8B95A5; font-size:11px; font-weight:700; letter-spacing:2px;");
        lay->addWidget(sec);

        m_eq = new QCheckBox(QStringLiteral("Equalizer"));
        m_eq->setToolTip(QStringLiteral("Arm the AudioHawk PipeWire sink and apply the active curve."));
        lay->addWidget(m_eq);

        auto *hint = new QLabel(QStringLiteral("Arm the AudioHawk PipeWire sink and apply the active curve."));
        hint->setStyleSheet("color:#8B95A5; font-size:13px;");
        hint->setWordWrap(true);
        lay->addWidget(hint);
    }

    /* Profiles */
    {
        auto *lay = makePanel(root);
        auto *sec = new QLabel(QStringLiteral("PROFILES"));
        sec->setStyleSheet("color:#8B95A5; font-size:11px; font-weight:700; letter-spacing:2px;");
        lay->addWidget(sec);

        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFixedHeight(64);

        auto *chipHost = new QWidget;
        auto *chipRow = new QHBoxLayout(chipHost);
        chipRow->setContentsMargins(0, 0, 0, 0);
        chipRow->setSpacing(8);

        const AhProfileDef *pdefs = ah_profile_defs();
        for (int i = 0; i < AH_PROFILE_COUNT; ++i) {
            auto *btn = new QPushButton(QString::fromUtf8(pdefs[i].name));
            btn->setProperty("class", QStringLiteral("ahChip"));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("active", false);
            btn->setStyleSheet(
                "QPushButton{background:#222833;border:1px solid #2C3440;border-radius:8px;"
                "color:#E8ECF1;font-weight:600;padding:10px 16px;}"
                "QPushButton:hover{border-color:#2EC4B6;}"
                "QPushButton[active=\"true\"]{background:rgba(46,196,182,40);border-color:#2EC4B6;color:#2EC4B6;}");
            m_chips[i] = btn;
            chipRow->addWidget(btn);
            connect(btn, &QPushButton::clicked, this, [this, i]() {
                ah_core_set_profile(m_core, static_cast<AhProfileId>(i));
                setChipActive(i);
                refreshBlurbs();
                refreshStatus();
            });
        }
        chipRow->addStretch();
        scroll->setWidget(chipHost);
        lay->addWidget(scroll);

        m_profileBlurb = new QLabel;
        m_profileBlurb->setWordWrap(true);
        m_profileBlurb->setStyleSheet("color:#8B95A5; font-size:13px;");
        lay->addWidget(m_profileBlurb);
    }

    /* Intelligent EQ */
    {
        auto *lay = makePanel(root);
        auto *sec = new QLabel(QStringLiteral("INTELLIGENT EQUALIZER"));
        sec->setStyleSheet("color:#8B95A5; font-size:11px; font-weight:700; letter-spacing:2px;");
        lay->addWidget(sec);

        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        const AhIntelDef *idefs = ah_intel_defs();
        for (int i = 0; i < AH_INTEL_COUNT; ++i) {
            auto *btn = new QPushButton(QString::fromUtf8(idefs[i].name));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("active", false);
            btn->setStyleSheet(
                "QPushButton{background:#222833;border:1px solid #2C3440;border-radius:8px;"
                "color:#E8ECF1;font-weight:600;padding:10px 16px;}"
                "QPushButton:hover{border-color:#E8A317;}"
                "QPushButton[active=\"true\"]{background:rgba(232,163,23,40);border-color:#E8A317;color:#E8A317;}");
            m_intel[i] = btn;
            row->addWidget(btn, 1);
            connect(btn, &QPushButton::clicked, this, [this, i]() {
                ah_core_set_intel(m_core, static_cast<AhIntelMode>(i));
                setIntelActive(i);
                refreshBlurbs();
                refreshStatus();
            });
        }
        lay->addLayout(row);

        m_intelBlurb = new QLabel;
        m_intelBlurb->setWordWrap(true);
        m_intelBlurb->setStyleSheet("color:#8B95A5; font-size:13px;");
        lay->addWidget(m_intelBlurb);
    }

    /* Per-app */
    {
        auto *lay = makePanel(root);
        auto *sec = new QLabel(QStringLiteral("PER APP AUDIO PROFILES"));
        sec->setStyleSheet("color:#8B95A5; font-size:11px; font-weight:700; letter-spacing:2px;");
        lay->addWidget(sec);

        m_autoSwitch = new QCheckBox(QStringLiteral("Auto-switch profiles"));
        lay->addWidget(m_autoSwitch);
        auto *aHint = new QLabel(QStringLiteral("Follow the focused app and apply its saved profile automatically."));
        aHint->setWordWrap(true);
        aHint->setStyleSheet("color:#8B95A5; font-size:13px;");
        lay->addWidget(aHint);

        m_deviceMemory = new QCheckBox(QStringLiteral("Per-device Audio Memory"));
        lay->addWidget(m_deviceMemory);
        auto *dHint = new QLabel(QStringLiteral("Remember EQ and profile choices for each output device."));
        dHint->setWordWrap(true);
        dHint->setStyleSheet("color:#8B95A5; font-size:13px;");
        lay->addWidget(dHint);

        auto *manage = new QPushButton(QStringLiteral("Manage App Profiles"));
        manage->setObjectName(QStringLiteral("ahManage"));
        manage->setCursor(Qt::PointingHandCursor);
        manage->setStyleSheet(
            "QPushButton{background:transparent;border:1px solid #2C3440;border-radius:8px;"
            "color:#E8ECF1;font-weight:600;padding:10px 16px;}"
            "QPushButton:hover{border-color:#E8A317;color:#E8A317;}");
        manage->setFixedWidth(220);
        lay->addWidget(manage, 0, Qt::AlignLeft);

        connect(manage, &QPushButton::clicked, this, [this]() {
            auto result = QMessageBox::question(
                this,
                QStringLiteral("Manage App Profiles"),
                QStringLiteral(
                    "Assign listening profiles to individual applications.\n"
                    "Entries live in ~/.config/audiohawk/app_profiles.conf.\n\n"
                    "Seed example mappings for firefox, spotify, and steam?"),
                QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::Yes) {
                ah_app_profiles_upsert(&m_core->app_profiles, "firefox", AH_PROFILE_MOVIE, true);
                ah_app_profiles_upsert(&m_core->app_profiles, "spotify", AH_PROFILE_MUSIC, true);
                ah_app_profiles_upsert(&m_core->app_profiles, "steam", AH_PROFILE_GAME, true);
                m_status->setText(QStringLiteral("Seeded example app profiles (firefox/spotify/steam)"));
            }
        });
    }

    /* Startup */
    {
        auto *lay = makePanel(root);
        auto *sec = new QLabel(QStringLiteral("STARTUP"));
        sec->setStyleSheet("color:#8B95A5; font-size:11px; font-weight:700; letter-spacing:2px;");
        lay->addWidget(sec);

        m_startup = new QCheckBox(QStringLiteral("Start on system startup"));
        lay->addWidget(m_startup);
        auto *sHint = new QLabel(QStringLiteral(
            "Launch AudioHawk in the background when you log in. EQ state is restored from your last session."));
        sHint->setWordWrap(true);
        sHint->setStyleSheet("color:#8B95A5; font-size:13px;");
        lay->addWidget(sHint);
    }

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("ahStatus"));
    m_status->setStyleSheet("color:#8B95A5; font-family:'IBM Plex Mono','Noto Sans Mono',monospace; font-size:12px;");
    root->addWidget(m_status);
    root->addStretch();

    connect(m_eq, &QCheckBox::toggled, this, [this](bool on) {
        if (m_suppress)
            return;
        if (ah_core_set_eq_enabled(m_core, on) != 0) {
            m_suppress = true;
            m_eq->setChecked(false);
            m_suppress = false;
            const char *err = ah_pw_eq_last_error(m_core->eq);
            m_status->setText(err && err[0] ? QString::fromUtf8(err)
                                            : QStringLiteral("Failed to arm PipeWire EQ"));
            return;
        }
        refreshStatus();
    });
    connect(m_autoSwitch, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_suppress)
            ah_core_set_auto_switch(m_core, on);
    });
    connect(m_deviceMemory, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_suppress)
            ah_core_set_per_device_memory(m_core, on);
    });
    connect(m_startup, &QCheckBox::toggled, this, [this](bool on) {
        if (m_suppress)
            return;
        if (ah_core_set_start_on_startup(m_core, on) != 0) {
            m_suppress = true;
            m_startup->setChecked(!on);
            m_suppress = false;
            m_status->setText(QStringLiteral("Could not update session autostart"));
        }
    });

    rebuildFromSettings();
}

void Dashboard::rebuildFromSettings()
{
    m_suppress = true;
    m_eq->setChecked(m_core->settings.eq_enabled);
    m_autoSwitch->setChecked(m_core->settings.auto_switch_profiles);
    m_deviceMemory->setChecked(m_core->settings.per_device_memory);
    m_startup->setChecked(m_core->settings.start_on_startup);
    m_suppress = false;
    setChipActive(static_cast<int>(m_core->settings.profile));
    setIntelActive(static_cast<int>(m_core->settings.intel));
    refreshBlurbs();
    refreshStatus();
}

void Dashboard::refreshBlurbs()
{
    const AhProfileDef *p = ah_profile_by_id(m_core->settings.profile);
    const AhIntelDef *i = ah_intel_by_id(m_core->settings.intel);
    m_profileBlurb->setText(QString::fromUtf8(p->blurb));
    m_intelBlurb->setText(QString::fromUtf8(i->blurb));
}

void Dashboard::refreshStatus()
{
    const AhProfileDef *p = ah_profile_by_id(m_core->settings.profile);
    const AhIntelDef *i = ah_intel_by_id(m_core->settings.intel);
    m_status->setText(QStringLiteral("sink:%1  ·  %2  ·  intel:%3  ·  %4")
                          .arg(QString::fromUtf8(ah_pw_eq_node_name()),
                               QString::fromUtf8(p->name),
                               QString::fromUtf8(i->name),
                               m_core->settings.eq_enabled ? QStringLiteral("ARMED")
                                                           : QStringLiteral("STANDBY")));
    m_rail->setProperty("armed", m_core->settings.eq_enabled);
    m_rail->setStyleSheet(m_core->settings.eq_enabled
                              ? "background:#E8A317; min-height:4px; max-height:4px; border-radius:2px;"
                              : "background:#8A6412; min-height:4px; max-height:4px; border-radius:2px;");
}

void Dashboard::setChipActive(int index)
{
    for (int i = 0; i < AH_PROFILE_COUNT; ++i) {
        m_chips[i]->setProperty("active", i == index);
        m_chips[i]->style()->unpolish(m_chips[i]);
        m_chips[i]->style()->polish(m_chips[i]);
        m_chips[i]->update();
    }
}

void Dashboard::setIntelActive(int index)
{
    for (int i = 0; i < AH_INTEL_COUNT; ++i) {
        m_intel[i]->setProperty("active", i == index);
        m_intel[i]->style()->unpolish(m_intel[i]);
        m_intel[i]->style()->polish(m_intel[i]);
        m_intel[i]->update();
    }
}
