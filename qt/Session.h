/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "RpcClient.h"
#include "Torrent.h"
#include "Typedefs.h"

class AddData;
class Prefs;

extern "C"
{
struct tr_variant;
}

class Session : public QObject
{
    Q_OBJECT

public:
    Session(QString const& config_dir, Prefs& prefs);
    virtual ~Session();

    void stop();
    void restart();

    QUrl const& getRemoteUrl() const
    {
        return rpc_.url();
    }

    tr_session_stats const& getStats() const
    {
        return stats_;
    }

    tr_session_stats const& getCumulativeStats() const
    {
        return cumulative_stats_;
    }

    QString const& sessionVersion() const
    {
        return session_version_;
    }

    int64_t blocklistSize() const
    {
        return blocklist_size_;
    }

    void setBlocklistSize(int64_t i);
    void updateBlocklist();
    void portTest();
    void copyMagnetLinkToClipboard(int torrent_id);

    /** returns true if the transmission session is being run inside this client */
    bool isServer() const;

    /** returns true if isServer() is true or if the remote address is the localhost */
    bool isLocal() const;

    RpcResponseFuture exec(tr_quark method, tr_variant* args);
    RpcResponseFuture exec(char const* method, tr_variant* args);

    void torrentSet(torrent_ids_t const& ids, tr_quark const key, bool val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, int val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, double val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, QList<int> const& val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, QStringList const& val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, QPair<int, QString> const& val);
    void torrentSetLocation(torrent_ids_t const& ids, QString const& path, bool do_move);
    void torrentRenamePath(torrent_ids_t const& ids, QString const& oldpath, QString const& newname);
    void addTorrent(AddData const& addme, tr_variant* top, bool trash_original);
    void initTorrents(torrent_ids_t const& ids = {});
    void pauseTorrents(torrent_ids_t const& torrentIds = {});
    void startTorrents(torrent_ids_t const& torrentIds = {});
    void startTorrentsNow(torrent_ids_t const& torrentIds = {});
    void refreshDetailInfo(torrent_ids_t const& torrent_ids);
    void refreshActiveTorrents();
    void refreshAllTorrents();
    void addNewlyCreatedTorrent(QString const& filename, QString const& local_path);
    void verifyTorrents(torrent_ids_t const& torrent_ids);
    void reannounceTorrents(torrent_ids_t const& torrent_ids);
    void refreshExtraStats(torrent_ids_t const& ids);

public slots:
    void addTorrent(AddData const& addme);
    void launchWebInterface();
    void queueMoveBottom(torrent_ids_t const& torrentIds = {});
    void queueMoveDown(torrent_ids_t const& torrentIds = {});
    void queueMoveTop(torrent_ids_t const& torrentIds = {});
    void queueMoveUp(torrent_ids_t const& torrentIds = {});
    void refreshSessionInfo();
    void refreshSessionStats();
    void removeTorrents(torrent_ids_t const& torrent_ids, bool delete_files = false);
    void updatePref(int key);

signals:
    void sourceChanged();
    void portTested(bool is_open);
    void statsUpdated();
    void sessionUpdated();
    void blocklistUpdated(int);
    void torrentsUpdated(tr_variant* torrent_list, bool complete_list);
    void torrentsRemoved(tr_variant* torrent_list);
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(QNetworkReply::NetworkError code, QString const& message);
    void httpAuthenticationRequired();

private:
    void start();

    void updateStats(tr_variant* args);
    void updateInfo(tr_variant* args);

    void sessionSet(tr_quark const key, QVariant const& variant);
    void pumpRequests();
    void sendTorrentRequest(char const* request, torrent_ids_t const& torrent_ids);
    void refreshTorrents(torrent_ids_t const& torrent_ids, Torrent::KeyList const& keys);

    static void updateStats(tr_variant* d, tr_session_stats* stats);

private:
    QString const config_dir_;
    Prefs& prefs_;

    int64_t blocklist_size_ = -1;
    tr_session* session_ = {};
    QStringList idle_json_;
    tr_session_stats stats_;
    tr_session_stats cumulative_stats_;
    QString session_version_;
    QString session_id_;
    bool is_definitely_local_session_ = true;
    RpcClient rpc_;
};
