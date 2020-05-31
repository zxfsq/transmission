/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <ctime>

#include <QMainWindow>
#include <QNetworkReply>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidgetList>

#include "Filters.h"
#include "Speed.h"
#include "TorrentFilter.h"
#include "Typedefs.h"

#include "ui_MainWindow.h"

class QAction;
class QIcon;
class QMenu;
class QStringList;

class AboutDialog;
class AddData;
class DetailsDialog;
class Prefs;
class PrefsDialog;
class Session;
class SessionDialog;
class StatsDialog;
class TorrentDelegate;
class TorrentDelegateMin;
class TorrentModel;

extern "C"
{
struct tr_variant;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Session&, Prefs&, TorrentModel&, bool minized);
    virtual ~MainWindow();

    QSystemTrayIcon& trayIcon()
    {
        return tray_icon_;
    }

public slots:
    void startAll();
    void startSelected();
    void startSelectedNow();
    void pauseAll();
    void pauseSelected();
    void removeSelected();
    void deleteSelected();
    void verifySelected();
    void queueMoveTop();
    void queueMoveUp();
    void queueMoveDown();
    void queueMoveBottom();
    void reannounceSelected();
    void onNetworkTimer();

    void setToolbarVisible(bool);
    void setFilterbarVisible(bool);
    void setStatusbarVisible(bool);
    void setCompactView(bool);
    void wrongAuthentication();

    void openSession();

protected:
    // QWidget
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    QIcon getStockIcon(QString const&, int fallback = -1);
    QIcon addEmblem(QIcon icon, QStringList const& emblem_names);

    torrent_ids_t getSelectedTorrents(bool withMetadataOnly = false) const;
    void updateNetworkIcon();

    QMenu* createOptionsMenu();
    QMenu* createStatsModeMenu();
    void initStatusBar();

    void clearSelection();
    void addTorrent(AddData const& add_me, bool show_options);

    // QWidget
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void addTorrents(QStringList const& filenames);
    void copyMagnetLinkToClipboard();
    void dataReadProgress();
    void dataSendProgress();
    void newTorrent();
    void onNetworkResponse(QNetworkReply::NetworkError code, QString const& message);
    void onRefreshTimer();
    void onSessionSourceChanged();
    void onSetPrefs();
    void onSetPrefs(bool);
    void onSortModeChanged(QAction* action);
    void onStatsModeChanged(QAction* action);
    void openAbout();
    void openDonate();
    void openFolder();
    void openHelp();
    void openPreferences();
    void openProperties();
    void openStats();
    void openTorrent();
    void openURL();
    void refreshPref(int key);
    void refreshSoon(int fields = ~0);
    void removeTorrents(bool const delete_files);
    void setLocation();
    void setSortAscendingPref(bool);
    void toggleSpeedMode();
    void toggleWindows(bool do_show);
    void trayActivated(QSystemTrayIcon::ActivationReason);

private:
    Session& session_;
    Prefs& prefs_;
    TorrentModel& model_;

    QPixmap pixmap_network_error_;
    QPixmap pixmap_network_idle_;
    QPixmap pixmap_network_receive_;
    QPixmap pixmap_network_transmit_;
    QPixmap pixmap_network_transmit_receive_;

    Ui_MainWindow ui_;

    time_t last_full_update_time_ = {};
    QPointer<SessionDialog> session_dialog_;
    QPointer<PrefsDialog> prefs_dialog_;
    QPointer<AboutDialog> about_dialog_;
    QPointer<StatsDialog> stats_dialog_;
    QPointer<DetailsDialog> details_dialog_;
    QSystemTrayIcon tray_icon_;
    TorrentFilter filter_model_;
    TorrentDelegate* torrent_delegate_ = {};
    TorrentDelegateMin* torrent_delegate_min_ = {};
    time_t last_send_time_ = {};
    time_t last_read_time_ = {};
    QTimer network_timer_;
    bool network_error_ = {};
    QAction* dlimit_off_action_ = {};
    QAction* dlimit_on_action_ = {};
    QAction* ulimit_off_action_ = {};
    QAction* ulimit_on_action_ = {};
    QAction* ratio_off_action_ = {};
    QAction* ratio_on_action_ = {};
    QWidgetList hidden_;
    QWidget* filter_bar_ = {};
    QAction* alt_speed_action_ = {};
    QString error_message_;

    struct TransferStats
    {
        Speed speed_up;
        Speed speed_down;
        size_t peers_sending = 0;
        size_t peers_receiving = 0;
    };
    TransferStats getTransferStats() const;

    enum
    {
        REFRESH_TITLE = (1 << 0),
        REFRESH_STATUS_BAR = (1 << 1),
        REFRESH_TRAY_ICON = (1 << 2),
        REFRESH_TORRENT_VIEW_HEADER = (1 << 3),
        REFRESH_ACTION_SENSITIVITY = (1 << 4)
    };
    int refresh_fields_ = {};
    QTimer refresh_timer_;

    void refreshActionSensitivity();
    void refreshStatusBar(TransferStats const&);
    void refreshTitle();
    void refreshTorrentViewHeader();
    void refreshTrayIcon(TransferStats const&);
};
