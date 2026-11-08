#include "settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "appsettings.h"
#include "autostart.h"

SettingsDialog::SettingsDialog(bool trayAvailable, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Настройки"));
    setMinimumWidth(420);

    m_language = new QComboBox(this);
    m_language->addItem(tr("Как в системе"), static_cast<int>(AppSettings::Language::System));
    m_language->addItem(tr("Русский"), static_cast<int>(AppSettings::Language::Russian));
    m_language->addItem(tr("English"), static_cast<int>(AppSettings::Language::English));
    m_language->setCurrentIndex(m_language->findData(static_cast<int>(AppSettings::language())));

    m_theme = new QComboBox(this);
    m_theme->addItem(tr("Как в системе"), static_cast<int>(AppSettings::Theme::System));
    m_theme->addItem(tr("Светлая"), static_cast<int>(AppSettings::Theme::Light));
    m_theme->addItem(tr("Тёмная"), static_cast<int>(AppSettings::Theme::Dark));
    m_theme->setCurrentIndex(m_theme->findData(static_cast<int>(AppSettings::theme())));

    m_autoCheck = new QCheckBox(tr("Автоматически проверять обновления (не чаще раза в сутки)"), this);
    m_autoCheck->setChecked(AppSettings::autoCheckUpdates());

    m_guiAutostart = new QCheckBox(tr("Запускать приложение при входе в систему"), this);
    m_guiAutostart->setChecked(Autostart::isEnabled());

    m_closeToTray = new QCheckBox(tr("Сворачивать в системный трей при закрытии окна"), this);
    m_closeToTray->setChecked(AppSettings::closeToTray());
    m_closeToTray->setEnabled(trayAvailable);

    auto *form = new QFormLayout;
    form->addRow(tr("Язык интерфейса:"), m_language);
    form->addRow(tr("Тема оформления:"), m_theme);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addSpacing(8);
    layout->addWidget(m_autoCheck);
    layout->addWidget(m_guiAutostart);
    layout->addWidget(m_closeToTray);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                             | QDialogButtonBox::Apply,
                                         this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (applySettings())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this] { applySettings(); });
}

bool SettingsDialog::applySettings()
{
    if (Autostart::isEnabled() != m_guiAutostart->isChecked()
        && !Autostart::setEnabled(m_guiAutostart->isChecked())) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось изменить автозапуск Zapret2 Control."));
        m_guiAutostart->setChecked(Autostart::isEnabled());
        return false;
    }

    const auto oldLanguage = AppSettings::language();
    const auto newLanguage = static_cast<AppSettings::Language>(m_language->currentData().toInt());
    const auto newTheme = static_cast<AppSettings::Theme>(m_theme->currentData().toInt());

    AppSettings::setLanguage(newLanguage);
    AppSettings::setTheme(newTheme);
    AppSettings::setAutoCheckUpdates(m_autoCheck->isChecked());
    AppSettings::setCloseToTray(m_closeToTray->isChecked());
    AppSettings::applyTheme(*qApp);

    if (oldLanguage != newLanguage)
        emit languageRestartRequired();
    return true;
}
