/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#ifdef _WIN32
#include <winsock2.h> // FD_SETSIZE
#else
#include <sys/select.h> // FD_SETSIZE
#endif

#include <cassert>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "ColumnResizer.h"
#include "FreeSpaceLabel.h"
#include "Formatter.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

/***
****
***/

namespace
{

class PreferenceWidget
{
    static char const* const PREF_KEY;

public:
    explicit PreferenceWidget(QObject* object) :
        m_object(object)
    {
    }

    template<typename T>
    bool is() const
    {
        return qobject_cast<T*>(m_object) != nullptr;
    }

    template<typename T>
    T const* as() const
    {
        assert(is<T>());
        return static_cast<T const*>(m_object);
    }

    template<typename T>
    T* as()
    {
        assert(is<T>());
        return static_cast<T*>(m_object);
    }

    void setPrefKey(int key)
    {
        m_object->setProperty(PREF_KEY, key);
    }

    int getPrefKey() const
    {
        return m_object->property(PREF_KEY).toInt();
    }

private:
    QObject* const m_object;
};

char const* const PreferenceWidget::PREF_KEY = "pref-key";

int qtDayToTrDay(int day)
{
    switch (day)
    {
    case Qt::Monday:
        return TR_SCHED_MON;

    case Qt::Tuesday:
        return TR_SCHED_TUES;

    case Qt::Wednesday:
        return TR_SCHED_WED;

    case Qt::Thursday:
        return TR_SCHED_THURS;

    case Qt::Friday:
        return TR_SCHED_FRI;

    case Qt::Saturday:
        return TR_SCHED_SAT;

    case Qt::Sunday:
        return TR_SCHED_SUN;

    default:
        assert(false && "Invalid day of week");
        return 0;
    }
}

QString qtDayName(int day)
{
    switch (day)
    {
    case Qt::Monday:
        return PrefsDialog::tr("Monday");

    case Qt::Tuesday:
        return PrefsDialog::tr("Tuesday");

    case Qt::Wednesday:
        return PrefsDialog::tr("Wednesday");

    case Qt::Thursday:
        return PrefsDialog::tr("Thursday");

    case Qt::Friday:
        return PrefsDialog::tr("Friday");

    case Qt::Saturday:
        return PrefsDialog::tr("Saturday");

    case Qt::Sunday:
        return PrefsDialog::tr("Sunday");

    default:
        assert(false && "Invalid day of week");
        return QString();
    }
}

} // namespace

bool PrefsDialog::updateWidgetValue(QWidget* widget, int pref_key)
{
    PreferenceWidget pref_widget(widget);

    if (pref_widget.is<QCheckBox>())
    {
        pref_widget.as<QCheckBox>()->setChecked(prefs_.getBool(pref_key));
    }
    else if (pref_widget.is<QSpinBox>())
    {
        pref_widget.as<QSpinBox>()->setValue(prefs_.getInt(pref_key));
    }
    else if (pref_widget.is<QDoubleSpinBox>())
    {
        pref_widget.as<QDoubleSpinBox>()->setValue(prefs_.getDouble(pref_key));
    }
    else if (pref_widget.is<QTimeEdit>())
    {
        pref_widget.as<QTimeEdit>()->setTime(QTime(0, 0).addSecs(prefs_.getInt(pref_key) * 60));
    }
    else if (pref_widget.is<QLineEdit>())
    {
        pref_widget.as<QLineEdit>()->setText(prefs_.getString(pref_key));
    }
    else if (pref_widget.is<PathButton>())
    {
        pref_widget.as<PathButton>()->setPath(prefs_.getString(pref_key));
    }
    else if (pref_widget.is<FreeSpaceLabel>())
    {
        pref_widget.as<FreeSpaceLabel>()->setPath(prefs_.getString(pref_key));
    }
    else
    {
        return false;
    }

    return true;
}

void PrefsDialog::linkWidgetToPref(QWidget* widget, int pref_key)
{
    PreferenceWidget pref_widget(widget);

    pref_widget.setPrefKey(pref_key);
    updateWidgetValue(widget, pref_key);
    widgets_.insert(pref_key, widget);

    if (pref_widget.is<QCheckBox>())
    {
        connect(widget, SIGNAL(toggled(bool)), SLOT(checkBoxToggled(bool)));
    }
    else if (pref_widget.is<QTimeEdit>())
    {
        connect(widget, SIGNAL(editingFinished()), SLOT(timeEditingFinished()));
    }
    else if (pref_widget.is<QLineEdit>())
    {
        connect(widget, SIGNAL(editingFinished()), SLOT(lineEditingFinished()));
    }
    else if (pref_widget.is<PathButton>())
    {
        connect(widget, SIGNAL(pathChanged(QString)), SLOT(pathChanged(QString)));
    }
    else if (pref_widget.is<QAbstractSpinBox>())
    {
        connect(widget, SIGNAL(editingFinished()), SLOT(spinBoxEditingFinished()));
    }
}

void PrefsDialog::checkBoxToggled(bool checked)
{
    PreferenceWidget const pref_widget(sender());

    if (pref_widget.is<QCheckBox>())
    {
        setPref(pref_widget.getPrefKey(), checked);
    }
}

void PrefsDialog::spinBoxEditingFinished()
{
    PreferenceWidget const pref_widget(sender());

    if (pref_widget.is<QDoubleSpinBox>())
    {
        setPref(pref_widget.getPrefKey(), pref_widget.as<QDoubleSpinBox>()->value());
    }
    else if (pref_widget.is<QSpinBox>())
    {
        setPref(pref_widget.getPrefKey(), pref_widget.as<QSpinBox>()->value());
    }
}

void PrefsDialog::timeEditingFinished()
{
    PreferenceWidget const pref_widget(sender());

    if (pref_widget.is<QTimeEdit>())
    {
        setPref(pref_widget.getPrefKey(), QTime(0, 0).secsTo(pref_widget.as<QTimeEdit>()->time()) / 60);
    }
}

void PrefsDialog::lineEditingFinished()
{
    PreferenceWidget const pref_widget(sender());

    if (pref_widget.is<QLineEdit>())
    {
        auto const* const lineEdit = pref_widget.as<QLineEdit>();

        if (lineEdit->isModified())
        {
            setPref(pref_widget.getPrefKey(), lineEdit->text());
        }
    }
}

void PrefsDialog::pathChanged(QString const& path)
{
    PreferenceWidget const pref_widget(sender());

    if (pref_widget.is<PathButton>())
    {
        setPref(pref_widget.getPrefKey(), path);
    }
}

/***
****
***/

void PrefsDialog::initRemoteTab()
{
    linkWidgetToPref(ui_.enableRpcCheck, Prefs::RPC_ENABLED);
    linkWidgetToPref(ui_.rpcPortSpin, Prefs::RPC_PORT);
    linkWidgetToPref(ui_.requireRpcAuthCheck, Prefs::RPC_AUTH_REQUIRED);
    linkWidgetToPref(ui_.rpcUsernameEdit, Prefs::RPC_USERNAME);
    linkWidgetToPref(ui_.rpcPasswordEdit, Prefs::RPC_PASSWORD);
    linkWidgetToPref(ui_.enableRpcWhitelistCheck, Prefs::RPC_WHITELIST_ENABLED);
    linkWidgetToPref(ui_.rpcWhitelistEdit, Prefs::RPC_WHITELIST);

    web_widgets_ << ui_.rpcPortLabel << ui_.rpcPortSpin << ui_.requireRpcAuthCheck << ui_.enableRpcWhitelistCheck;
    web_auth_widgets_ << ui_.rpcUsernameLabel << ui_.rpcUsernameEdit << ui_.rpcPasswordLabel << ui_.rpcPasswordEdit;
    web_whitelist_widgets_ << ui_.rpcWhitelistLabel << ui_.rpcWhitelistEdit;
    unsupported_when_remote_ << ui_.enableRpcCheck << web_widgets_ << web_auth_widgets_ << web_whitelist_widgets_;

    connect(ui_.openWebClientButton, SIGNAL(clicked()), &session_, SLOT(launchWebInterface()));
}

/***
****
***/

void PrefsDialog::altSpeedDaysEdited(int i)
{
    int const value = qobject_cast<QComboBox*>(sender())->itemData(i).toInt();
    setPref(Prefs::ALT_SPEED_LIMIT_TIME_DAY, value);
}

void PrefsDialog::initSpeedTab()
{
    QString const speed_K_str = Formatter::unitStr(Formatter::SPEED, Formatter::KB);
    QLocale const locale;

    ui_.uploadSpeedLimitSpin->setSuffix(QStringLiteral(" %1").arg(speed_K_str));
    ui_.downloadSpeedLimitSpin->setSuffix(QStringLiteral(" %1").arg(speed_K_str));
    ui_.altUploadSpeedLimitSpin->setSuffix(QStringLiteral(" %1").arg(speed_K_str));
    ui_.altDownloadSpeedLimitSpin->setSuffix(QStringLiteral(" %1").arg(speed_K_str));

    ui_.altSpeedLimitDaysCombo->addItem(tr("Every Day"), QVariant(TR_SCHED_ALL));
    ui_.altSpeedLimitDaysCombo->addItem(tr("Weekdays"), QVariant(TR_SCHED_WEEKDAY));
    ui_.altSpeedLimitDaysCombo->addItem(tr("Weekends"), QVariant(TR_SCHED_WEEKEND));
    ui_.altSpeedLimitDaysCombo->insertSeparator(ui_.altSpeedLimitDaysCombo->count());

    for (int i = locale.firstDayOfWeek(); i <= Qt::Sunday; ++i)
    {
        ui_.altSpeedLimitDaysCombo->addItem(qtDayName(i), qtDayToTrDay(i));
    }

    for (int i = Qt::Monday; i < locale.firstDayOfWeek(); ++i)
    {
        ui_.altSpeedLimitDaysCombo->addItem(qtDayName(i), qtDayToTrDay(i));
    }

    ui_.altSpeedLimitDaysCombo->setCurrentIndex(ui_.altSpeedLimitDaysCombo->findData(prefs_.getInt(
        Prefs::ALT_SPEED_LIMIT_TIME_DAY)));

    linkWidgetToPref(ui_.uploadSpeedLimitCheck, Prefs::USPEED_ENABLED);
    linkWidgetToPref(ui_.uploadSpeedLimitSpin, Prefs::USPEED);
    linkWidgetToPref(ui_.downloadSpeedLimitCheck, Prefs::DSPEED_ENABLED);
    linkWidgetToPref(ui_.downloadSpeedLimitSpin, Prefs::DSPEED);
    linkWidgetToPref(ui_.altUploadSpeedLimitSpin, Prefs::ALT_SPEED_LIMIT_UP);
    linkWidgetToPref(ui_.altDownloadSpeedLimitSpin, Prefs::ALT_SPEED_LIMIT_DOWN);
    linkWidgetToPref(ui_.altSpeedLimitScheduleCheck, Prefs::ALT_SPEED_LIMIT_TIME_ENABLED);
    linkWidgetToPref(ui_.altSpeedLimitStartTimeEdit, Prefs::ALT_SPEED_LIMIT_TIME_BEGIN);
    linkWidgetToPref(ui_.altSpeedLimitEndTimeEdit, Prefs::ALT_SPEED_LIMIT_TIME_END);

    sched_widgets_ << ui_.altSpeedLimitStartTimeEdit << ui_.altSpeedLimitToLabel << ui_.altSpeedLimitEndTimeEdit <<
        ui_.altSpeedLimitDaysLabel << ui_.altSpeedLimitDaysCombo;

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.speedLimitsSectionLayout);
    cr->addLayout(ui_.altSpeedLimitsSectionLayout);
    cr->update();

    connect(ui_.altSpeedLimitDaysCombo, SIGNAL(activated(int)), SLOT(altSpeedDaysEdited(int)));
}

/***
****
***/

void PrefsDialog::initDesktopTab()
{
    linkWidgetToPref(ui_.showTrayIconCheck, Prefs::SHOW_TRAY_ICON);
    linkWidgetToPref(ui_.startMinimizedCheck, Prefs::START_MINIMIZED);
    linkWidgetToPref(ui_.notifyOnTorrentAddedCheck, Prefs::SHOW_NOTIFICATION_ON_ADD);
    linkWidgetToPref(ui_.notifyOnTorrentCompletedCheck, Prefs::SHOW_NOTIFICATION_ON_COMPLETE);
    linkWidgetToPref(ui_.playSoundOnTorrentCompletedCheck, Prefs::COMPLETE_SOUND_ENABLED);
}

/***
****
***/

void PrefsDialog::onPortTested(bool isOpen)
{
    ui_.testPeerPortButton->setEnabled(true);
    widgets_[Prefs::PEER_PORT]->setEnabled(true);
    ui_.peerPortStatusLabel->setText(isOpen ? tr("Port is <b>open</b>") : tr("Port is <b>closed</b>"));
}

void PrefsDialog::onPortTest()
{
    ui_.peerPortStatusLabel->setText(tr("Testing TCP Port..."));
    ui_.testPeerPortButton->setEnabled(false);
    widgets_[Prefs::PEER_PORT]->setEnabled(false);
    session_.portTest();
}

void PrefsDialog::initNetworkTab()
{
    ui_.torrentPeerLimitSpin->setRange(1, FD_SETSIZE);
    ui_.globalPeerLimitSpin->setRange(1, FD_SETSIZE);

    linkWidgetToPref(ui_.peerPortSpin, Prefs::PEER_PORT);
    linkWidgetToPref(ui_.randomPeerPortCheck, Prefs::PEER_PORT_RANDOM_ON_START);
    linkWidgetToPref(ui_.enablePortForwardingCheck, Prefs::PORT_FORWARDING);
    linkWidgetToPref(ui_.torrentPeerLimitSpin, Prefs::PEER_LIMIT_TORRENT);
    linkWidgetToPref(ui_.globalPeerLimitSpin, Prefs::PEER_LIMIT_GLOBAL);
    linkWidgetToPref(ui_.enableUtpCheck, Prefs::UTP_ENABLED);
    linkWidgetToPref(ui_.enablePexCheck, Prefs::PEX_ENABLED);
    linkWidgetToPref(ui_.enableDhtCheck, Prefs::DHT_ENABLED);
    linkWidgetToPref(ui_.enableLpdCheck, Prefs::LPD_ENABLED);

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.incomingPeersSectionLayout);
    cr->addLayout(ui_.peerLimitsSectionLayout);
    cr->update();

    connect(ui_.testPeerPortButton, SIGNAL(clicked()), SLOT(onPortTest()));
    connect(&session_, SIGNAL(portTested(bool)), SLOT(onPortTested(bool)));
}

/***
****
***/

void PrefsDialog::onBlocklistDialogDestroyed(QObject* o)
{
    Q_UNUSED(o)

    blocklist_dialog_ = nullptr;
}

void PrefsDialog::onUpdateBlocklistCancelled()
{
    disconnect(&session_, SIGNAL(blocklistUpdated(int)), this, SLOT(onBlocklistUpdated(int)));
    blocklist_dialog_->deleteLater();
}

void PrefsDialog::onBlocklistUpdated(int n)
{
    blocklist_dialog_->setText(tr("<b>Update succeeded!</b><p>Blocklist now has %Ln rule(s).", nullptr, n));
    blocklist_dialog_->setTextFormat(Qt::RichText);
}

void PrefsDialog::onUpdateBlocklistClicked()
{
    blocklist_dialog_ = new QMessageBox(QMessageBox::Information, QString(),
        tr("<b>Update Blocklist</b><p>Getting new blocklist..."), QMessageBox::Close, this);
    connect(blocklist_dialog_, SIGNAL(rejected()), this, SLOT(onUpdateBlocklistCancelled()));
    connect(&session_, SIGNAL(blocklistUpdated(int)), this, SLOT(onBlocklistUpdated(int)));
    blocklist_dialog_->show();
    session_.updateBlocklist();
}

void PrefsDialog::encryptionEdited(int i)
{
    int const value(qobject_cast<QComboBox*>(sender())->itemData(i).toInt());
    setPref(Prefs::ENCRYPTION, value);
}

void PrefsDialog::initPrivacyTab()
{
    ui_.encryptionModeCombo->addItem(tr("Allow encryption"), 0);
    ui_.encryptionModeCombo->addItem(tr("Prefer encryption"), 1);
    ui_.encryptionModeCombo->addItem(tr("Require encryption"), 2);

    linkWidgetToPref(ui_.encryptionModeCombo, Prefs::ENCRYPTION);
    linkWidgetToPref(ui_.blocklistCheck, Prefs::BLOCKLIST_ENABLED);
    linkWidgetToPref(ui_.blocklistEdit, Prefs::BLOCKLIST_URL);
    linkWidgetToPref(ui_.autoUpdateBlocklistCheck, Prefs::BLOCKLIST_UPDATES_ENABLED);

    block_widgets_ << ui_.blocklistEdit << ui_.blocklistStatusLabel << ui_.updateBlocklistButton <<
        ui_.autoUpdateBlocklistCheck;

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.encryptionSectionLayout);
    cr->addLayout(ui_.blocklistSectionLayout);
    cr->update();

    connect(ui_.encryptionModeCombo, SIGNAL(activated(int)), SLOT(encryptionEdited(int)));
    connect(ui_.updateBlocklistButton, SIGNAL(clicked()), SLOT(onUpdateBlocklistClicked()));

    updateBlocklistLabel();
}

/***
****
***/

void PrefsDialog::onIdleLimitChanged()
{
    //: Spin box suffix, "Stop seeding if idle for: [ 5 minutes ]" (includes leading space after the number, if needed)
    QString const units_suffix = tr(" minute(s)", nullptr, ui_.idleLimitSpin->value());

    if (ui_.idleLimitSpin->suffix() != units_suffix)
    {
        ui_.idleLimitSpin->setSuffix(units_suffix);
    }
}

void PrefsDialog::initSeedingTab()
{
    linkWidgetToPref(ui_.ratioLimitCheck, Prefs::RATIO_ENABLED);
    linkWidgetToPref(ui_.ratioLimitSpin, Prefs::RATIO);
    linkWidgetToPref(ui_.idleLimitCheck, Prefs::IDLE_LIMIT_ENABLED);
    linkWidgetToPref(ui_.idleLimitSpin, Prefs::IDLE_LIMIT);

    connect(ui_.idleLimitSpin, SIGNAL(valueChanged(int)), SLOT(onIdleLimitChanged()));

    onIdleLimitChanged();
}

void PrefsDialog::onQueueStalledMinutesChanged()
{
    //: Spin box suffix, "Download is inactive if data sharing stopped: [ 5 minutes ago ]" (includes leading space after the number, if needed)
    QString const units_suffix = tr(" minute(s) ago", nullptr, ui_.queueStalledMinutesSpin->value());

    if (ui_.queueStalledMinutesSpin->suffix() != units_suffix)
    {
        ui_.queueStalledMinutesSpin->setSuffix(units_suffix);
    }
}

void PrefsDialog::initDownloadingTab()
{
    ui_.watchDirButton->setMode(PathButton::DirectoryMode);
    ui_.downloadDirButton->setMode(PathButton::DirectoryMode);
    ui_.incompleteDirButton->setMode(PathButton::DirectoryMode);
    ui_.completionScriptButton->setMode(PathButton::FileMode);

    ui_.watchDirButton->setTitle(tr("Select Watch Directory"));
    ui_.downloadDirButton->setTitle(tr("Select Destination"));
    ui_.incompleteDirButton->setTitle(tr("Select Incomplete Directory"));
    ui_.completionScriptButton->setTitle(tr("Select \"Torrent Done\" Script"));

    ui_.watchDirStack->setMinimumWidth(200);

    ui_.downloadDirFreeSpaceLabel->setSession(session_);
    ui_.downloadDirFreeSpaceLabel->setPath(prefs_.getString(Prefs::DOWNLOAD_DIR));

    linkWidgetToPref(ui_.watchDirCheck, Prefs::DIR_WATCH_ENABLED);
    linkWidgetToPref(ui_.watchDirButton, Prefs::DIR_WATCH);
    linkWidgetToPref(ui_.watchDirEdit, Prefs::DIR_WATCH);
    linkWidgetToPref(ui_.showTorrentOptionsDialogCheck, Prefs::OPTIONS_PROMPT);
    linkWidgetToPref(ui_.startAddedTorrentsCheck, Prefs::START);
    linkWidgetToPref(ui_.trashTorrentFileCheck, Prefs::TRASH_ORIGINAL);
    linkWidgetToPref(ui_.downloadDirButton, Prefs::DOWNLOAD_DIR);
    linkWidgetToPref(ui_.downloadDirEdit, Prefs::DOWNLOAD_DIR);
    linkWidgetToPref(ui_.downloadDirFreeSpaceLabel, Prefs::DOWNLOAD_DIR);
    linkWidgetToPref(ui_.downloadQueueSizeSpin, Prefs::DOWNLOAD_QUEUE_SIZE);
    linkWidgetToPref(ui_.queueStalledMinutesSpin, Prefs::QUEUE_STALLED_MINUTES);
    linkWidgetToPref(ui_.renamePartialFilesCheck, Prefs::RENAME_PARTIAL_FILES);
    linkWidgetToPref(ui_.incompleteDirCheck, Prefs::INCOMPLETE_DIR_ENABLED);
    linkWidgetToPref(ui_.incompleteDirButton, Prefs::INCOMPLETE_DIR);
    linkWidgetToPref(ui_.incompleteDirEdit, Prefs::INCOMPLETE_DIR);
    linkWidgetToPref(ui_.completionScriptCheck, Prefs::SCRIPT_TORRENT_DONE_ENABLED);
    linkWidgetToPref(ui_.completionScriptButton, Prefs::SCRIPT_TORRENT_DONE_FILENAME);
    linkWidgetToPref(ui_.completionScriptEdit, Prefs::SCRIPT_TORRENT_DONE_FILENAME);

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.addingSectionLayout);
    cr->addLayout(ui_.downloadQueueSectionLayout);
    cr->addLayout(ui_.incompleteSectionLayout);
    cr->update();

    connect(ui_.queueStalledMinutesSpin, SIGNAL(valueChanged(int)), SLOT(onQueueStalledMinutesChanged()));

    updateDownloadingWidgetsLocality();
    onQueueStalledMinutesChanged();
}

void PrefsDialog::updateDownloadingWidgetsLocality()
{
    ui_.watchDirStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.watchDirButton) : ui_.watchDirEdit);
    ui_.downloadDirStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.downloadDirButton) : ui_.downloadDirEdit);
    ui_.incompleteDirStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.incompleteDirButton) : ui_.incompleteDirEdit);
    ui_.completionScriptStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.completionScriptButton) :
        ui_.completionScriptEdit);

    ui_.watchDirStack->setFixedHeight(ui_.watchDirStack->currentWidget()->sizeHint().height());
    ui_.downloadDirStack->setFixedHeight(ui_.downloadDirStack->currentWidget()->sizeHint().height());
    ui_.incompleteDirStack->setFixedHeight(ui_.incompleteDirStack->currentWidget()->sizeHint().height());
    ui_.completionScriptStack->setFixedHeight(ui_.completionScriptStack->currentWidget()->sizeHint().height());

    ui_.downloadDirLabel->setBuddy(ui_.downloadDirStack->currentWidget());
}

/***
****
***/

PrefsDialog::PrefsDialog(Session& session, Prefs& prefs, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    prefs_(prefs),
    is_server_(session.isServer()),
    is_local_(session_.isLocal())
{
    ui_.setupUi(this);

    initSpeedTab();
    initDownloadingTab();
    initSeedingTab();
    initPrivacyTab();
    initNetworkTab();
    initDesktopTab();
    initRemoteTab();

    connect(&session_, SIGNAL(sessionUpdated()), SLOT(sessionUpdated()));

    QList<int> keys;
    keys << Prefs::RPC_ENABLED << Prefs::ALT_SPEED_LIMIT_ENABLED << Prefs::ALT_SPEED_LIMIT_TIME_ENABLED << Prefs::ENCRYPTION <<
        Prefs::BLOCKLIST_ENABLED << Prefs::DIR_WATCH << Prefs::DOWNLOAD_DIR << Prefs::INCOMPLETE_DIR <<
        Prefs::INCOMPLETE_DIR_ENABLED << Prefs::SCRIPT_TORRENT_DONE_FILENAME;

    for (int const key : keys)
    {
        refreshPref(key);
    }

    // if it's a remote session, disable the preferences
    // that don't work in remote sessions
    if (!is_server_)
    {
        for (QWidget* const w : unsupported_when_remote_)
        {
            w->setToolTip(tr("Not supported by remote sessions"));
            w->setEnabled(false);
        }
    }

    adjustSize();
}

PrefsDialog::~PrefsDialog() = default;

void PrefsDialog::setPref(int key, QVariant const& v)
{
    prefs_.set(key, v);
    refreshPref(key);
}

/***
****
***/

void PrefsDialog::sessionUpdated()
{
    bool const is_local = session_.isLocal();

    if (is_local_ != is_local)
    {
        is_local_ = is_local;
        updateDownloadingWidgetsLocality();
    }

    updateBlocklistLabel();
}

void PrefsDialog::updateBlocklistLabel()
{
    int const n = session_.blocklistSize();
    ui_.blocklistStatusLabel->setText(tr("<i>Blocklist contains %Ln rule(s)</i>", nullptr, n));
}

void PrefsDialog::refreshPref(int key)
{
    switch (key)
    {
    case Prefs::RPC_ENABLED:
    case Prefs::RPC_WHITELIST_ENABLED:
    case Prefs::RPC_AUTH_REQUIRED:
        {
            bool const enabled(prefs_.getBool(Prefs::RPC_ENABLED));
            bool const whitelist(prefs_.getBool(Prefs::RPC_WHITELIST_ENABLED));
            bool const auth(prefs_.getBool(Prefs::RPC_AUTH_REQUIRED));

            for (QWidget* const w : web_whitelist_widgets_)
            {
                w->setEnabled(enabled && whitelist);
            }

            for (QWidget* const w : web_auth_widgets_)
            {
                w->setEnabled(enabled && auth);
            }

            for (QWidget* const w : web_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case Prefs::ALT_SPEED_LIMIT_TIME_ENABLED:
        {
            bool const enabled = prefs_.getBool(key);

            for (QWidget* const w : sched_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case Prefs::BLOCKLIST_ENABLED:
        {
            bool const enabled = prefs_.getBool(key);

            for (QWidget* const w : block_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case Prefs::PEER_PORT:
        ui_.peerPortStatusLabel->setText(tr("Status unknown"));
        ui_.testPeerPortButton->setEnabled(true);
        break;

    default:
        break;
    }

    key2widget_t::iterator it(widgets_.find(key));

    if (it != widgets_.end())
    {
        QWidget* w(it.value());

        if (!updateWidgetValue(w, key))
        {
            if (key == Prefs::ENCRYPTION)
            {
                auto* comboBox = qobject_cast<QComboBox*>(w);
                int const index = comboBox->findData(prefs_.getInt(key));
                comboBox->setCurrentIndex(index);
            }
        }
    }
}
