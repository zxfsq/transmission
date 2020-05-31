/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm> // std::any_of
#include <cassert>
#include <climits> /* INT_MAX */
#include <ctime>

#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHostAddress>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QResizeEvent>
#include <QStringList>
#include <QStyle>
#include <QTreeWidgetItem>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_getRatio()

#include "ColumnResizer.h"
#include "DetailsDialog.h"
#include "Formatter.h"
#include "Prefs.h"
#include "Session.h"
#include "SqueezeLabel.h"
#include "Torrent.h"
#include "TorrentModel.h"
#include "TrackerDelegate.h"
#include "TrackerModel.h"
#include "TrackerModelFilter.h"
#include "Utils.h"

class Prefs;
class Session;

/****
*****
****/

namespace
{

int const REFRESH_INTERVAL_MSEC = 4000;

char const* PREF_KEY("pref-key");

enum // peer columns
{
    COL_LOCK,
    COL_UP,
    COL_DOWN,
    COL_PERCENT,
    COL_STATUS,
    COL_ADDRESS,
    COL_CLIENT,
    N_COLUMNS
};

int measureViewItem(QTreeWidget* view, int column, QString const& text)
{
    QTreeWidgetItem const* header_item = view->headerItem();

    int const item_width = Utils::measureViewItem(view, text);
    int const header_width = Utils::measureHeaderItem(view->header(), header_item->text(column));

    return std::max(item_width, header_width);
}

} // namespace

/***
****
***/

class PeerItem : public QTreeWidgetItem
{
    Peer peer;
    QString mutable collated_address;
    QString status;

public:
    PeerItem(Peer const& p) :
        peer(p)
    {
    }

    ~PeerItem() override = default;

    void refresh(Peer const& p)
    {
        if (p.address != peer.address)
        {
            collated_address.clear();
        }

        peer = p;
    }

    void setStatus(QString const& s)
    {
        status = s;
    }

    bool operator <(QTreeWidgetItem const& other) const override
    {
        auto const* i = dynamic_cast<PeerItem const*>(&other);
        QTreeWidget* tw(treeWidget());
        int const column = tw != nullptr ? tw->sortColumn() : 0;

        assert(i != nullptr);

        switch (column)
        {
        case COL_UP:
            return peer.rate_to_peer < i->peer.rate_to_peer;

        case COL_DOWN:
            return peer.rate_to_client < i->peer.rate_to_client;

        case COL_PERCENT:
            return peer.progress < i->peer.progress;

        case COL_STATUS:
            return status < i->status;

        case COL_CLIENT:
            return peer.client_name < i->peer.client_name;

        case COL_LOCK:
            return peer.is_encrypted && !i->peer.is_encrypted;

        default:
            return address() < i->address();
        }
    }

private:
    QString const& address() const
    {
        if (collated_address.isEmpty())
        {
            QHostAddress ip_address;

            if (ip_address.setAddress(peer.address))
            {
                if (ip_address.protocol() == QAbstractSocket::IPv4Protocol)
                {
                    quint32 const ipv4_address = ip_address.toIPv4Address();
                    collated_address = QStringLiteral("1-") + QString::fromUtf8(QByteArray::number(ipv4_address, 16).
                        rightJustified(8, '0'));
                }
                else if (ip_address.protocol() == QAbstractSocket::IPv6Protocol)
                {
                    Q_IPV6ADDR const ipv6_address = ip_address.toIPv6Address();
                    QByteArray tmp(16, '\0');

                    for (int i = 0; i < 16; ++i)
                    {
                        tmp[i] = ipv6_address[i];
                    }

                    collated_address = QStringLiteral("2-") + QString::fromUtf8(tmp.toHex());
                }
            }

            if (collated_address.isEmpty())
            {
                collated_address = QStringLiteral("3-") + peer.address.toLower();
            }
        }

        return collated_address;
    }
};

/***
****
***/

QIcon DetailsDialog::getStockIcon(QString const& freedesktop_name, int fallback)
{
    QIcon icon = QIcon::fromTheme(freedesktop_name);

    if (icon.isNull())
    {
        icon = style()->standardIcon(QStyle::StandardPixmap(fallback), nullptr, this);
    }

    return icon;
}

DetailsDialog::DetailsDialog(Session& session, Prefs& prefs, TorrentModel const& model, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    prefs_(prefs),
    model_(model)
{
    ui_.setupUi(this);

    initInfoTab();
    initPeersTab();
    initTrackerTab();
    initFilesTab();
    initOptionsTab();

    adjustSize();
    ui_.commentBrowser->setMaximumHeight(QWIDGETSIZE_MAX);

    QList<int> init_keys;
    init_keys << Prefs::SHOW_TRACKER_SCRAPES << Prefs::SHOW_BACKUP_TRACKERS;

    for (int const key : init_keys)
    {
        refreshPref(key);
    }

    connect(&model_, &TorrentModel::torrentsChanged, this, &DetailsDialog::onTorrentsChanged);
    connect(&prefs_, &Prefs::changed, this, &DetailsDialog::refreshPref);
    connect(&timer_, &QTimer::timeout, this, &DetailsDialog::onTimer);

    onTimer();
    timer_.setSingleShot(false);
    timer_.start(REFRESH_INTERVAL_MSEC);
}

DetailsDialog::~DetailsDialog()
{
    tracker_delegate_->deleteLater();
    tracker_filter_->deleteLater();
    tracker_model_->deleteLater();
}

void DetailsDialog::setIds(torrent_ids_t const& ids)
{
    if (ids != ids_)
    {
        setEnabled(false);
        ui_.filesView->clear();

        ids_ = ids;
        session_.refreshDetailInfo(ids_);
        changed_torrents_ = true;
        tracker_model_->refresh(model_, ids_);
        onTimer();
    }
}

void DetailsDialog::refreshPref(int key)
{
    QString str;

    switch (key)
    {
    case Prefs::SHOW_TRACKER_SCRAPES:
        {
            QItemSelectionModel* selection_model(ui_.trackersView->selectionModel());
            QItemSelection const selection(selection_model->selection());
            QModelIndex const current_index(selection_model->currentIndex());
            tracker_delegate_->setShowMore(prefs_.getBool(key));
            selection_model->clear();
            ui_.trackersView->reset();
            selection_model->select(selection, QItemSelectionModel::Select);
            selection_model->setCurrentIndex(current_index, QItemSelectionModel::NoUpdate);
            break;
        }

    case Prefs::SHOW_BACKUP_TRACKERS:
        tracker_filter_->setShowBackupTrackers(prefs_.getBool(key));
        break;

    default:
        break;
    }
}

/***
****
***/

void DetailsDialog::onTimer()
{
    getNewData();
}

void DetailsDialog::getNewData()
{
    if (!ids_.empty())
    {
        session_.refreshExtraStats(ids_);
    }
}

void DetailsDialog::onTorrentEdited(torrent_ids_t const& /*ids*/)
{
    // FIXME
    // refreshDetailInfo({ tor.id() });
}

void DetailsDialog::onTorrentsChanged(torrent_ids_t const& ids)
{
    if (have_pending_refresh_)
    {
        return;
    }

    if (!std::any_of(ids.begin(), ids.end(), [this](auto const& id) { return ids_.count(id) != 0; }))
    {
        return;
    }

    have_pending_refresh_ = true;
    QTimer::singleShot(100, this, SLOT(refresh()));
}

namespace
{

void setIfIdle(QComboBox* box, int i)
{
    if (!box->hasFocus())
    {
        box->blockSignals(true);
        box->setCurrentIndex(i);
        box->blockSignals(false);
    }
}

void setIfIdle(QDoubleSpinBox* spin, double value)
{
    if (!spin->hasFocus())
    {
        spin->blockSignals(true);
        spin->setValue(value);
        spin->blockSignals(false);
    }
}

void setIfIdle(QSpinBox* spin, int value)
{
    if (!spin->hasFocus())
    {
        spin->blockSignals(true);
        spin->setValue(value);
        spin->blockSignals(false);
    }
}

} // namespace

void DetailsDialog::refresh()
{
    int const n = ids_.size();
    bool const single = n == 1;
    QString const blank;
    QFontMetrics const fm(fontMetrics());
    QList<Torrent const*> torrents;
    QString string;
    QString const none = tr("None");
    QString const mixed = tr("Mixed");
    QString const unknown = tr("Unknown");

    // build a list of torrents
    for (int const id : ids_)
    {
        Torrent const* tor = model_.getTorrentFromId(id);

        if (tor != nullptr)
        {
            torrents << tor;
        }
    }

    ///
    ///  activity tab
    ///

    // myStateLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        bool is_mixed = false;
        bool all_paused = true;
        bool all_finished = true;
        tr_torrent_activity const baseline = torrents[0]->getActivity();

        for (Torrent const* const t : torrents)
        {
            tr_torrent_activity const activity = t->getActivity();

            if (activity != baseline)
            {
                is_mixed = true;
            }

            if (activity != TR_STATUS_STOPPED)
            {
                all_paused = all_finished = false;
            }

            if (!t->isFinished())
            {
                all_finished = false;
            }
        }

        if (is_mixed)
        {
            string = mixed;
        }
        else if (all_finished)
        {
            string = tr("Finished");
        }
        else if (all_paused)
        {
            string = tr("Paused");
        }
        else
        {
            string = torrents[0]->activityString();
        }
    }

    ui_.stateValueLabel->setText(string);
    QString const state_string = string;

    // myHaveLabel
    uint64_t size_when_done = 0;
    uint64_t available = 0;

    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        uint64_t left_until_done = 0;
        int64_t have_total = 0;
        int64_t have_verified = 0;
        int64_t have_unverified = 0;
        int64_t verified_pieces = 0;

        for (Torrent const* const t : torrents)
        {
            if (t->hasMetadata())
            {
                have_total += t->haveTotal();
                have_unverified += t->haveUnverified();
                uint64_t const v = t->haveVerified();
                have_verified += v;

                if (t->pieceSize())
                {
                    verified_pieces += v / t->pieceSize();
                }

                size_when_done += t->sizeWhenDone();
                left_until_done += t->leftUntilDone();
                available += t->sizeWhenDone() - t->leftUntilDone() + t->desiredAvailable();
            }
        }

        double const d = size_when_done != 0 ? 100.0 * (size_when_done - left_until_done) / size_when_done : 100.0;
        QString pct = Formatter::percentToString(d);

        if (have_unverified == 0 && left_until_done == 0)
        {
            //: Text following the "Have:" label in torrent properties dialog;
            //: %1 is amount of downloaded and verified data
            string = tr("%1 (100%)").arg(Formatter::sizeToString(have_verified));
        }
        else if (have_unverified == 0)
        {
            //: Text following the "Have:" label in torrent properties dialog;
            //: %1 is amount of downloaded and verified data,
            //: %2 is overall size of torrent data,
            //: %3 is percentage (%1/%2*100)
            string = tr("%1 of %2 (%3%)").arg(Formatter::sizeToString(have_verified)).arg(Formatter::sizeToString(
                size_when_done)).
                arg(pct);
        }
        else
        {
            //: Text following the "Have:" label in torrent properties dialog;
            //: %1 is amount of downloaded data (both verified and unverified),
            //: %2 is overall size of torrent data,
            //: %3 is percentage (%1/%2*100),
            //: %4 is amount of downloaded but not yet verified data
            string = tr("%1 of %2 (%3%), %4 Unverified").arg(Formatter::sizeToString(have_verified + have_unverified)).
                arg(Formatter::sizeToString(size_when_done)).arg(pct).arg(Formatter::sizeToString(have_unverified));
        }
    }

    ui_.haveValueLabel->setText(string);

    // myAvailabilityLabel
    if (torrents.empty() || size_when_done == 0)
    {
        string = none;
    }
    else
    {
        string = QStringLiteral("%1%").arg(Formatter::percentToString((100.0 * available) / size_when_done));
    }

    ui_.availabilityValueLabel->setText(string);

    // myDownloadedLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        uint64_t d = 0;
        uint64_t f = 0;

        for (Torrent const* const t : torrents)
        {
            d += t->downloadedEver();
            f += t->failedEver();
        }

        QString const dstr = Formatter::sizeToString(d);
        QString const fstr = Formatter::sizeToString(f);

        if (f != 0)
        {
            string = tr("%1 (%2 corrupt)").arg(dstr).arg(fstr);
        }
        else
        {
            string = dstr;
        }
    }

    ui_.downloadedValueLabel->setText(string);

    //  myUploadedLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        uint64_t u = 0;
        uint64_t d = 0;

        for (Torrent const* const t : torrents)
        {
            u += t->uploadedEver();
            d += t->downloadedEver();
        }

        string = tr("%1 (Ratio: %2)").arg(Formatter::sizeToString(u)).arg(Formatter::ratioToString(tr_getRatio(u, d)));
    }

    ui_.uploadedValueLabel->setText(string);

    // myRunTimeLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        bool all_paused = true;
        auto baseline = torrents[0]->lastStarted();

        for (Torrent const* const t : torrents)
        {
            if (baseline != t->lastStarted())
            {
                baseline = 0;
            }

            if (!t->isPaused())
            {
                all_paused = false;
            }
        }

        if (all_paused)
        {
            string = state_string; // paused || finished
        }
        else if (baseline == 0)
        {
            string = mixed;
        }
        else
        {
            auto const now = time(nullptr);
            auto const seconds = int(std::difftime(now, baseline));
            string = Formatter::timeToString(seconds);
        }
    }

    ui_.runningTimeValueLabel->setText(string);

    // myETALabel
    string.clear();

    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        int baseline = torrents[0]->getETA();

        for (Torrent const* const t : torrents)
        {
            if (baseline != t->getETA())
            {
                string = mixed;
                break;
            }
        }

        if (string.isEmpty())
        {
            if (baseline < 0)
            {
                string = tr("Unknown");
            }
            else
            {
                string = Formatter::timeToString(baseline);
            }
        }
    }

    ui_.remainingTimeValueLabel->setText(string);

    // myLastActivityLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        auto latest = torrents[0]->lastActivity();

        for (Torrent const* const t : torrents)
        {
            auto const dt = t->lastActivity();

            if (latest < dt)
            {
                latest = dt;
            }
        }

        auto const now = time(nullptr);
        auto const seconds = int(std::difftime(now, latest));

        if (seconds < 0)
        {
            string = none;
        }
        else if (seconds < 5)
        {
            string = tr("Active now");
        }
        else
        {
            string = tr("%1 ago").arg(Formatter::timeToString(seconds));
        }
    }

    ui_.lastActivityValueLabel->setText(string);

    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        string = torrents[0]->getError();

        for (Torrent const* const t : torrents)
        {
            if (string != t->getError())
            {
                string = mixed;
                break;
            }
        }
    }

    if (string.isEmpty())
    {
        string = none;
    }

    ui_.errorValueLabel->setText(string);

    ///
    /// information tab
    ///

    // mySizeLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        int pieces = 0;
        uint64_t size = 0;
        uint32_t piece_size = torrents[0]->pieceSize();

        for (Torrent const* const t : torrents)
        {
            pieces += t->pieceCount();
            size += t->totalSize();

            if (piece_size != t->pieceSize())
            {
                piece_size = 0;
            }
        }

        if (size == 0)
        {
            string = none;
        }
        else if (piece_size > 0)
        {
            string = tr("%1 (%Ln pieces @ %2)", "", pieces).arg(Formatter::sizeToString(size)).
                arg(Formatter::memToString(piece_size));
        }
        else
        {
            string = tr("%1 (%Ln pieces)", "", pieces).arg(Formatter::sizeToString(size));
        }
    }

    ui_.sizeValueLabel->setText(string);

    // myHashLabel
    string = none;

    if (!torrents.empty())
    {
        string = torrents[0]->hashString();

        for (Torrent const* const t : torrents)
        {
            if (string != t->hashString())
            {
                string = mixed;
                break;
            }
        }
    }

    ui_.hashValueLabel->setText(string);

    // myPrivacyLabel
    string = none;

    if (!torrents.empty())
    {
        bool b = torrents[0]->isPrivate();
        string = b ? tr("Private to this tracker -- DHT and PEX disabled") : tr("Public torrent");

        for (Torrent const* const t : torrents)
        {
            if (b != t->isPrivate())
            {
                string = mixed;
                break;
            }
        }
    }

    ui_.privacyValueLabel->setText(string);

    // myCommentBrowser
    string = none;
    bool isCommentMixed = false;

    if (!torrents.empty())
    {
        string = torrents[0]->comment();

        for (Torrent const* const t : torrents)
        {
            if (string != t->comment())
            {
                string = mixed;
                isCommentMixed = true;
                break;
            }
        }
    }

    if (ui_.commentBrowser->toPlainText() != string)
    {
        ui_.commentBrowser->setText(string);
    }

    ui_.commentBrowser->setEnabled(!isCommentMixed && !string.isEmpty());

    // myOriginLabel
    string = none;

    if (!torrents.empty())
    {
        bool mixed_creator = false;
        bool mixed_date = false;
        QString const creator = torrents[0]->creator();
        auto const date = torrents[0]->dateCreated();

        for (Torrent const* const t : torrents)
        {
            mixed_creator |= (creator != t->creator());
            mixed_date |= (date != t->dateCreated());
        }

        bool const empty_creator = creator.isEmpty();
        bool const empty_date = date <= 0;

        if (mixed_creator || mixed_date)
        {
            string = mixed;
        }
        else if (empty_creator && empty_date)
        {
            string = tr("N/A");
        }
        else if (empty_date && !empty_creator)
        {
            string = tr("Created by %1").arg(creator);
        }
        else if (empty_creator && !empty_date)
        {
            auto const date_str = QDateTime::fromSecsSinceEpoch(date).toString();
            string = tr("Created on %1").arg(date_str);
        }
        else
        {
            auto const date_str = QDateTime::fromSecsSinceEpoch(date).toString();
            string = tr("Created by %1 on %2").arg(creator).arg(date_str);
        }
    }

    ui_.originValueLabel->setText(string);

    // myLocationLabel
    string = none;

    if (!torrents.empty())
    {
        string = torrents[0]->getPath();

        for (Torrent const* const t : torrents)
        {
            if (string != t->getPath())
            {
                string = mixed;
                break;
            }
        }
    }

    ui_.locationValueLabel->setText(string);

    ///
    ///  Options Tab
    ///

    if (changed_torrents_ && !torrents.empty())
    {
        int i;
        bool uniform;
        bool baseline_flag;
        int baseline_int;
        Torrent const& baseline = *torrents.front();

        // mySessionLimitCheck
        uniform = true;
        baseline_flag = baseline.honorsSessionLimits();

        for (Torrent const* const tor : torrents)
        {
            if (baseline_flag != tor->honorsSessionLimits())
            {
                uniform = false;
                break;
            }
        }

        ui_.sessionLimitCheck->setChecked(uniform && baseline_flag);

        // mySingleDownCheck
        uniform = true;
        baseline_flag = baseline.downloadIsLimited();

        for (Torrent const* const tor : torrents)
        {
            if (baseline_flag != tor->downloadIsLimited())
            {
                uniform = false;
                break;
            }
        }

        ui_.singleDownCheck->setChecked(uniform && baseline_flag);

        // mySingleUpCheck
        uniform = true;
        baseline_flag = baseline.uploadIsLimited();

        for (Torrent const* const tor : torrents)
        {
            if (baseline_flag != tor->uploadIsLimited())
            {
                uniform = false;
                break;
            }
        }

        ui_.singleUpCheck->setChecked(uniform && baseline_flag);

        // myBandwidthPriorityCombo
        uniform = true;
        baseline_int = baseline.getBandwidthPriority();

        for (Torrent const* const tor : torrents)
        {
            if (baseline_int != tor->getBandwidthPriority())
            {
                uniform = false;
                break;
            }
        }

        if (uniform)
        {
            i = ui_.bandwidthPriorityCombo->findData(baseline_int);
        }
        else
        {
            i = -1;
        }

        setIfIdle(ui_.bandwidthPriorityCombo, i);

        setIfIdle(ui_.singleDownSpin, int(baseline.downloadLimit().KBps()));
        setIfIdle(ui_.singleUpSpin, int(baseline.uploadLimit().KBps()));
        setIfIdle(ui_.peerLimitSpin, baseline.peerLimit());
    }

    if (!torrents.empty())
    {
        Torrent const& baseline = *torrents.front();

        // ratio
        bool uniform = true;
        int baseline_int = baseline.seedRatioMode();

        for (Torrent const* const tor : torrents)
        {
            if (baseline_int != tor->seedRatioMode())
            {
                uniform = false;
                break;
            }
        }

        setIfIdle(ui_.ratioCombo, uniform ? ui_.ratioCombo->findData(baseline_int) : -1);
        ui_.ratioSpin->setVisible(uniform && baseline_int == TR_RATIOLIMIT_SINGLE);

        setIfIdle(ui_.ratioSpin, baseline.seedRatioLimit());

        // idle
        uniform = true;
        baseline_int = baseline.seedIdleMode();

        for (Torrent const* const tor : torrents)
        {
            if (baseline_int != tor->seedIdleMode())
            {
                uniform = false;
                break;
            }
        }

        setIfIdle(ui_.idleCombo, uniform ? ui_.idleCombo->findData(baseline_int) : -1);
        ui_.idleSpin->setVisible(uniform && baseline_int == TR_RATIOLIMIT_SINGLE);

        setIfIdle(ui_.idleSpin, baseline.seedIdleLimit());
        onIdleLimitChanged();
    }

    ///
    ///  Tracker tab
    ///

    tracker_model_->refresh(model_, ids_);

    ///
    ///  Peers tab
    ///

    QMap<QString, QTreeWidgetItem*> peers2;
    QList<QTreeWidgetItem*> newItems;

    for (Torrent const* const t : torrents)
    {
        QString const idStr(QString::number(t->id()));
        PeerList peers = t->peers();

        for (Peer const& peer : peers)
        {
            QString const key = idStr + QLatin1Char(':') + peer.address;
            PeerItem* item = static_cast<PeerItem*>(peers_.value(key, nullptr));

            if (item == nullptr) // new peer has connected
            {
                static QIcon const ENCRYPTION_ICON(QStringLiteral(":/icons/encrypted.png"));
                static QIcon const EMPTY_ICON;
                item = new PeerItem(peer);
                item->setTextAlignment(COL_UP, Qt::AlignRight | Qt::AlignVCenter);
                item->setTextAlignment(COL_DOWN, Qt::AlignRight | Qt::AlignVCenter);
                item->setTextAlignment(COL_PERCENT, Qt::AlignRight | Qt::AlignVCenter);
                item->setIcon(COL_LOCK, peer.is_encrypted ? ENCRYPTION_ICON : EMPTY_ICON);
                item->setToolTip(COL_LOCK, peer.is_encrypted ? tr("Encrypted connection") : QString());
                item->setText(COL_ADDRESS, peer.address);
                item->setText(COL_CLIENT, peer.client_name);
                newItems << item;
            }

            QString const code = peer.flags;
            item->setStatus(code);
            item->refresh(peer);

            QString code_tip;

            for (QChar const ch : code)
            {
                QString txt;

                switch (ch.unicode())
                {
                case 'O':
                    txt = tr("Optimistic unchoke");
                    break;

                case 'D':
                    txt = tr("Downloading from this peer");
                    break;

                case 'd':
                    txt = tr("We would download from this peer if they would let us");
                    break;

                case 'U':
                    txt = tr("Uploading to peer");
                    break;

                case 'u':
                    txt = tr("We would upload to this peer if they asked");
                    break;

                case 'K':
                    txt = tr("Peer has unchoked us, but we're not interested");
                    break;

                case '?':
                    txt = tr("We unchoked this peer, but they're not interested");
                    break;

                case 'E':
                    txt = tr("Encrypted connection");
                    break;

                case 'H':
                    txt = tr("Peer was discovered through DHT");
                    break;

                case 'X':
                    txt = tr("Peer was discovered through Peer Exchange (PEX)");
                    break;

                case 'I':
                    txt = tr("Peer is an incoming connection");
                    break;

                case 'T':
                    txt = tr("Peer is connected over uTP");
                    break;
                }

                if (!txt.isEmpty())
                {
                    code_tip += QStringLiteral("%1: %2\n").arg(ch).arg(txt);
                }
            }

            if (!code_tip.isEmpty())
            {
                code_tip.resize(code_tip.size() - 1); // eat the trailing linefeed
            }

            item->setText(COL_UP, peer.rate_to_peer.isZero() ? QString() : Formatter::speedToString(peer.rate_to_peer));
            item->setText(COL_DOWN, peer.rate_to_client.isZero() ? QString() : Formatter::speedToString(peer.rate_to_client));
            item->setText(COL_PERCENT, peer.progress > 0 ? QStringLiteral("%1%").arg(int(peer.progress * 100.0)) :
                QString());
            item->setText(COL_STATUS, code);
            item->setToolTip(COL_STATUS, code_tip);

            peers2.insert(key, item);
        }
    }

    ui_.peersView->addTopLevelItems(newItems);

    for (QString const& key : peers_.keys())
    {
        if (!peers2.contains(key)) // old peer has disconnected
        {
            QTreeWidgetItem* item = peers_.value(key, nullptr);
            ui_.peersView->takeTopLevelItem(ui_.peersView->indexOfTopLevelItem(item));
            delete item;
        }
    }

    peers_ = peers2;

    if (!single)
    {
        ui_.filesView->clear();
    }

    if (single)
    {
        ui_.filesView->update(torrents[0]->files(), changed_torrents_);
    }

    changed_torrents_ = false;
    have_pending_refresh_ = false;
    setEnabled(true);
}

void DetailsDialog::setEnabled(bool enabled)
{
    for (int i = 0; i < ui_.tabs->count(); ++i)
    {
        ui_.tabs->widget(i)->setEnabled(enabled);
    }
}

/***
****
***/

void DetailsDialog::initInfoTab()
{
    int const h = QFontMetrics(ui_.commentBrowser->font()).lineSpacing() * 4;
    ui_.commentBrowser->setFixedHeight(h);

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.activitySectionLayout);
    cr->addLayout(ui_.detailsSectionLayout);
    cr->update();
}

/***
****
***/

void DetailsDialog::onShowTrackerScrapesToggled(bool val)
{
    prefs_.set(Prefs::SHOW_TRACKER_SCRAPES, val);
}

void DetailsDialog::onShowBackupTrackersToggled(bool val)
{
    prefs_.set(Prefs::SHOW_BACKUP_TRACKERS, val);
}

void DetailsDialog::onHonorsSessionLimitsToggled(bool val)
{
    session_.torrentSet(ids_, TR_KEY_honorsSessionLimits, val);
    getNewData();
}

void DetailsDialog::onDownloadLimitedToggled(bool val)
{
    session_.torrentSet(ids_, TR_KEY_downloadLimited, val);
    getNewData();
}

void DetailsDialog::onSpinBoxEditingFinished()
{
    QObject const* spin = sender();
    tr_quark const key = spin->property(PREF_KEY).toInt();
    auto const* d = qobject_cast<QDoubleSpinBox const*>(spin);

    if (d != nullptr)
    {
        session_.torrentSet(ids_, key, d->value());
    }
    else
    {
        session_.torrentSet(ids_, key, qobject_cast<QSpinBox const*>(spin)->value());
    }

    getNewData();
}

void DetailsDialog::onUploadLimitedToggled(bool val)
{
    session_.torrentSet(ids_, TR_KEY_uploadLimited, val);
    getNewData();
}

void DetailsDialog::onIdleModeChanged(int index)
{
    int const val = ui_.idleCombo->itemData(index).toInt();
    session_.torrentSet(ids_, TR_KEY_seedIdleMode, val);
    getNewData();
}

void DetailsDialog::onIdleLimitChanged()
{
    //: Spin box suffix, "Stop seeding if idle for: [ 5 minutes ]" (includes leading space after the number, if needed)
    QString const unitsSuffix = tr(" minute(s)", nullptr, ui_.idleSpin->value());

    if (ui_.idleSpin->suffix() != unitsSuffix)
    {
        ui_.idleSpin->setSuffix(unitsSuffix);
    }
}

void DetailsDialog::onRatioModeChanged(int index)
{
    int const val = ui_.ratioCombo->itemData(index).toInt();
    session_.torrentSet(ids_, TR_KEY_seedRatioMode, val);
}

void DetailsDialog::onBandwidthPriorityChanged(int index)
{
    if (index != -1)
    {
        int const priority = ui_.bandwidthPriorityCombo->itemData(index).toInt();
        session_.torrentSet(ids_, TR_KEY_bandwidthPriority, priority);
        getNewData();
    }
}

void DetailsDialog::onTrackerSelectionChanged()
{
    int const selection_count = ui_.trackersView->selectionModel()->selectedRows().size();
    ui_.editTrackerButton->setEnabled(selection_count == 1);
    ui_.removeTrackerButton->setEnabled(selection_count > 0);
}

void DetailsDialog::onAddTrackerClicked()
{
    bool ok = false;
    QString const url = QInputDialog::getText(this, tr("Add URL "), tr("Add tracker announce URL:"), QLineEdit::Normal,
        QString(), &ok);

    if (!ok)
    {
        // user pressed "cancel" -- noop
    }
    else if (!QUrl(url).isValid())
    {
        QMessageBox::warning(this, tr("Error"), tr("Invalid URL \"%1\"").arg(url));
    }
    else
    {
        torrent_ids_t ids;

        for (int const id : ids_)
        {
            if (tracker_model_->find(id, url) == -1)
            {
                ids.insert(id);
            }
        }

        if (ids.empty()) // all the torrents already have this tracker
        {
            QMessageBox::warning(this, tr("Error"), tr("Tracker already exists."));
        }
        else
        {
            auto const urls = QStringList{ url };
            session_.torrentSet(ids, TR_KEY_trackerAdd, urls);
            getNewData();
        }
    }
}

void DetailsDialog::onEditTrackerClicked()
{
    QItemSelectionModel* selection_model = ui_.trackersView->selectionModel();
    QModelIndexList selected_rows = selection_model->selectedRows();
    assert(selected_rows.size() == 1);
    QModelIndex i = selection_model->currentIndex();
    auto const trackerInfo = ui_.trackersView->model()->data(i, TrackerModel::TrackerRole).value<TrackerInfo>();

    bool ok = false;
    QString const newval = QInputDialog::getText(this, tr("Edit URL "), tr("Edit tracker announce URL:"), QLineEdit::Normal,
        trackerInfo.st.announce, &ok);

    if (!ok)
    {
        // user pressed "cancel" -- noop
    }
    else if (!QUrl(newval).isValid())
    {
        QMessageBox::warning(this, tr("Error"), tr("Invalid URL \"%1\"").arg(newval));
    }
    else
    {
        torrent_ids_t ids{ trackerInfo.torrent_id };

        QPair<int, QString> const id_url = qMakePair(trackerInfo.st.id, newval);

        session_.torrentSet(ids, TR_KEY_trackerReplace, id_url);
        getNewData();
    }
}

void DetailsDialog::onRemoveTrackerClicked()
{
    // make a map of torrentIds to announce URLs to remove
    QItemSelectionModel* selection_model = ui_.trackersView->selectionModel();
    QModelIndexList selected_rows = selection_model->selectedRows();
    QMap<int, int> torrent_id_to_tracker_ids;

    for (QModelIndex const& i : selected_rows)
    {
        auto const inf = ui_.trackersView->model()->data(i, TrackerModel::TrackerRole).value<TrackerInfo>();
        torrent_id_to_tracker_ids.insertMulti(inf.torrent_id, inf.st.id);
    }

    // batch all of a tracker's torrents into one command
    for (int const id : torrent_id_to_tracker_ids.uniqueKeys())
    {
        torrent_ids_t const ids{ id };
        session_.torrentSet(ids, TR_KEY_trackerRemove, torrent_id_to_tracker_ids.values(id));
    }

    selection_model->clearSelection();
    getNewData();
}

void DetailsDialog::initOptionsTab()
{
    QString const speed_K_str = Formatter::unitStr(Formatter::SPEED, Formatter::KB);

    ui_.singleDownSpin->setSuffix(QStringLiteral(" %1").arg(speed_K_str));
    ui_.singleUpSpin->setSuffix(QStringLiteral(" %1").arg(speed_K_str));

    ui_.singleDownSpin->setProperty(PREF_KEY, TR_KEY_downloadLimit);
    ui_.singleUpSpin->setProperty(PREF_KEY, TR_KEY_uploadLimit);
    ui_.ratioSpin->setProperty(PREF_KEY, TR_KEY_seedRatioLimit);
    ui_.idleSpin->setProperty(PREF_KEY, TR_KEY_seedIdleLimit);
    ui_.peerLimitSpin->setProperty(PREF_KEY, TR_KEY_peer_limit);

    ui_.bandwidthPriorityCombo->addItem(tr("High"), TR_PRI_HIGH);
    ui_.bandwidthPriorityCombo->addItem(tr("Normal"), TR_PRI_NORMAL);
    ui_.bandwidthPriorityCombo->addItem(tr("Low"), TR_PRI_LOW);

    ui_.ratioCombo->addItem(tr("Use Global Settings"), TR_RATIOLIMIT_GLOBAL);
    ui_.ratioCombo->addItem(tr("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED);
    ui_.ratioCombo->addItem(tr("Stop seeding at ratio:"), TR_RATIOLIMIT_SINGLE);

    ui_.idleCombo->addItem(tr("Use Global Settings"), TR_IDLELIMIT_GLOBAL);
    ui_.idleCombo->addItem(tr("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED);
    ui_.idleCombo->addItem(tr("Stop seeding if idle for:"), TR_IDLELIMIT_SINGLE);

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.speedSectionLayout);
    cr->addLayout(ui_.seedingLimitsSectionRatioLayout);
    cr->addLayout(ui_.seedingLimitsSectionIdleLayout);
    cr->addLayout(ui_.peerConnectionsSectionLayout);
    cr->update();

    void (QComboBox::* comboIndexChanged)(int) = &QComboBox::currentIndexChanged;
    void (QSpinBox::* spinValueChanged)(int) = &QSpinBox::valueChanged;
    connect(ui_.bandwidthPriorityCombo, comboIndexChanged, this, &DetailsDialog::onBandwidthPriorityChanged);
    connect(ui_.idleCombo, comboIndexChanged, this, &DetailsDialog::onIdleModeChanged);
    connect(ui_.idleSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui_.idleSpin, spinValueChanged, this, &DetailsDialog::onIdleLimitChanged);
    connect(ui_.peerLimitSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui_.ratioCombo, comboIndexChanged, this, &DetailsDialog::onRatioModeChanged);
    connect(ui_.ratioSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui_.sessionLimitCheck, &QCheckBox::clicked, this, &DetailsDialog::onHonorsSessionLimitsToggled);
    connect(ui_.singleDownCheck, &QCheckBox::clicked, this, &DetailsDialog::onDownloadLimitedToggled);
    connect(ui_.singleDownSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui_.singleUpCheck, &QCheckBox::clicked, this, &DetailsDialog::onUploadLimitedToggled);
    connect(ui_.singleUpSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
}

/***
****
***/

void DetailsDialog::initTrackerTab()
{
    tracker_model_ = new TrackerModel();
    tracker_filter_ = new TrackerModelFilter();
    tracker_filter_->setSourceModel(tracker_model_);
    tracker_delegate_ = new TrackerDelegate();

    ui_.trackersView->setModel(tracker_filter_);
    ui_.trackersView->setItemDelegate(tracker_delegate_);

    ui_.addTrackerButton->setIcon(getStockIcon(QStringLiteral("list-add"), QStyle::SP_DialogOpenButton));
    ui_.editTrackerButton->setIcon(getStockIcon(QStringLiteral("document-properties"), QStyle::SP_DesktopIcon));
    ui_.removeTrackerButton->setIcon(getStockIcon(QStringLiteral("list-remove"), QStyle::SP_TrashIcon));

    ui_.showTrackerScrapesCheck->setChecked(prefs_.getBool(Prefs::SHOW_TRACKER_SCRAPES));
    ui_.showBackupTrackersCheck->setChecked(prefs_.getBool(Prefs::SHOW_BACKUP_TRACKERS));

    connect(ui_.addTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onAddTrackerClicked);
    connect(ui_.editTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onEditTrackerClicked);
    connect(ui_.removeTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onRemoveTrackerClicked);
    connect(ui_.showBackupTrackersCheck, &QAbstractButton::clicked, this, &DetailsDialog::onShowBackupTrackersToggled);
    connect(ui_.showTrackerScrapesCheck, &QAbstractButton::clicked, this, &DetailsDialog::onShowTrackerScrapesToggled);
    connect(
        ui_.trackersView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        &DetailsDialog::onTrackerSelectionChanged);

    onTrackerSelectionChanged();
}

/***
****
***/

void DetailsDialog::initPeersTab()
{
    ui_.peersView->setHeaderLabels({ QString(), tr("Up"), tr("Down"), tr("%"), tr("Status"), tr("Address"), tr("Client") });
    ui_.peersView->sortByColumn(COL_ADDRESS, Qt::AscendingOrder);

    ui_.peersView->setColumnWidth(COL_LOCK, 20);
    ui_.peersView->setColumnWidth(COL_UP, measureViewItem(ui_.peersView, COL_UP, QStringLiteral("1024 MiB/s")));
    ui_.peersView->setColumnWidth(COL_DOWN, measureViewItem(ui_.peersView, COL_DOWN, QStringLiteral("1024 MiB/s")));
    ui_.peersView->setColumnWidth(COL_PERCENT, measureViewItem(ui_.peersView, COL_PERCENT, QStringLiteral("100%")));
    ui_.peersView->setColumnWidth(COL_STATUS, measureViewItem(ui_.peersView, COL_STATUS, QStringLiteral("ODUK?EXI")));
    ui_.peersView->setColumnWidth(COL_ADDRESS, measureViewItem(ui_.peersView, COL_ADDRESS, QStringLiteral("888.888.888.888")));
}

/***
****
***/

void DetailsDialog::initFilesTab()
{
    connect(ui_.filesView, &FileTreeView::openRequested, this, &DetailsDialog::onOpenRequested);
    connect(ui_.filesView, &FileTreeView::pathEdited, this, &DetailsDialog::onPathEdited);
    connect(ui_.filesView, &FileTreeView::priorityChanged, this, &DetailsDialog::onFilePriorityChanged);
    connect(ui_.filesView, &FileTreeView::wantedChanged, this, &DetailsDialog::onFileWantedChanged);
}

void DetailsDialog::onFilePriorityChanged(QSet<int> const& indices, int priority)
{
    tr_quark key;

    switch (priority)
    {
    case TR_PRI_LOW:
        key = TR_KEY_priority_low;
        break;

    case TR_PRI_HIGH:
        key = TR_KEY_priority_high;
        break;

    default:
        key = TR_KEY_priority_normal;
        break;
    }

    session_.torrentSet(ids_, key, indices.values());
    getNewData();
}

void DetailsDialog::onFileWantedChanged(QSet<int> const& indices, bool wanted)
{
    tr_quark const key = wanted ? TR_KEY_files_wanted : TR_KEY_files_unwanted;
    session_.torrentSet(ids_, key, indices.values());
    getNewData();
}

void DetailsDialog::onPathEdited(QString const& oldpath, QString const& newname)
{
    session_.torrentRenamePath(ids_, oldpath, newname);
}

void DetailsDialog::onOpenRequested(QString const& path)
{
    if (!session_.isLocal())
    {
        return;
    }

    for (int const id : ids_)
    {
        Torrent const* const tor = model_.getTorrentFromId(id);

        if (tor == nullptr)
        {
            continue;
        }

        QString const localFilePath = tor->getPath() + QLatin1Char('/') + path;

        if (!QFile::exists(localFilePath))
        {
            continue;
        }

        if (QDesktopServices::openUrl(QUrl::fromLocalFile(localFilePath)))
        {
            break;
        }
    }
}
