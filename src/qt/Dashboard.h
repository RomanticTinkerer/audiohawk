#pragma once

#include "audiohawk/core.h"

#include <QWidget>

class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;

class Dashboard : public QWidget {
    Q_OBJECT
public:
    explicit Dashboard(AhCore *core, QWidget *parent = nullptr);

private:
    void rebuildFromSettings();
    void refreshBlurbs();
    void refreshStatus();
    void setChipActive(int index);
    void setIntelActive(int index);

    AhCore *m_core = nullptr;
    QFrame *m_rail = nullptr;
    QCheckBox *m_eq = nullptr;
    QCheckBox *m_autoSwitch = nullptr;
    QCheckBox *m_deviceMemory = nullptr;
    QCheckBox *m_startup = nullptr;
    QLabel *m_profileBlurb = nullptr;
    QLabel *m_intelBlurb = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_chips[AH_PROFILE_COUNT]{};
    QPushButton *m_intel[AH_INTEL_COUNT]{};
    bool m_suppress = false;
};
