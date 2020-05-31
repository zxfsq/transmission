/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QStyleOptionProgressBar>

#include "Formatter.h"
#include "Torrent.h"
#include "TorrentDelegate.h"
#include "TorrentModel.h"
#include "Utils.h"

enum
{
    GUI_PAD = 6,
    BAR_HEIGHT = 12
};

QColor TorrentDelegate::green_brush_;
QColor TorrentDelegate::blue_brush_;
QColor TorrentDelegate::silver_brush_;
QColor TorrentDelegate::green_back_;
QColor TorrentDelegate::blue_back_;
QColor TorrentDelegate::silver_back_;

namespace
{

class ItemLayout
{
private:
    QString name_text_;
    QString status_text_;
    QString progress_text_;

public:
    QFont name_font;
    QFont status_font;
    QFont progress_font;

    QRect icon_rect;
    QRect emblem_rect;
    QRect name_rect;
    QRect status_rect;
    QRect bar_rect;
    QRect progress_rect;

    ItemLayout(QString const& name_text, QString const& status_text, QString const& progress_text, QIcon const& emblem_icon,
        QFont const& base_font, Qt::LayoutDirection direction, QPoint const& top_left, int width);

    QSize size() const
    {
        return (icon_rect | name_rect | status_rect | bar_rect | progress_rect).size();
    }

    QString nameText() const
    {
        return elidedText(name_font, name_text_, name_rect.width());
    }

    QString statusText() const
    {
        return elidedText(status_font, status_text_, status_rect.width());
    }

    QString progressText() const
    {
        return elidedText(progress_font, progress_text_, progress_rect.width());
    }

private:
    QString elidedText(QFont const& font, QString const& text, int width) const
    {
        return QFontMetrics(font).elidedText(text, Qt::ElideRight, width);
    }
};

ItemLayout::ItemLayout(QString const& name_text, QString const& status_text, QString const& progress_text,
    QIcon const& emblem_icon, QFont const& base_font, Qt::LayoutDirection direction, QPoint const& top_left,
    int width) :
    name_text_(name_text),
    status_text_(status_text),
    progress_text_(progress_text),
    name_font(base_font),
    status_font(base_font),
    progress_font(base_font)
{
    QStyle const* style(qApp->style());
    int const icon_size(style->pixelMetric(QStyle::PM_LargeIconSize));

    name_font.setWeight(QFont::Bold);
    QFontMetrics const name_fm(name_font);
    QSize const name_size(name_fm.size(0, name_text_));

    status_font.setPointSize(static_cast<int>(status_font.pointSize() * 0.9));
    QFontMetrics const status_fm(status_font);
    QSize const status_size(status_fm.size(0, status_text_));

    progress_font.setPointSize(static_cast<int>(progress_font.pointSize() * 0.9));
    QFontMetrics const progress_fm(progress_font);
    QSize const progress_size(progress_fm.size(0, progress_text_));

    QRect base_rect(top_left, QSize(width, 0));
    Utils::narrowRect(base_rect, icon_size + GUI_PAD, 0, direction);

    name_rect = base_rect.adjusted(0, 0, 0, name_size.height());
    status_rect = name_rect.adjusted(0, name_rect.height() + 1, 0, status_size.height() + 1);
    bar_rect = status_rect.adjusted(0, status_rect.height() + 1, 0, BAR_HEIGHT + 1);
    progress_rect = bar_rect.adjusted(0, bar_rect.height() + 1, 0, progress_size.height() + 1);
    icon_rect = style->alignedRect(direction, Qt::AlignLeft | Qt::AlignVCenter, QSize(icon_size, icon_size),
        QRect(top_left, QSize(width, progress_rect.bottom() - name_rect.top())));
    emblem_rect = style->alignedRect(direction, Qt::AlignRight | Qt::AlignBottom,
        emblem_icon.actualSize(icon_rect.size() / 2, QIcon::Normal, QIcon::On), icon_rect);
}

} // namespace

TorrentDelegate::TorrentDelegate(QObject* parent) :
    QStyledItemDelegate(parent),
    progress_bar_style_(new QStyleOptionProgressBar)
{
    progress_bar_style_->minimum = 0;
    progress_bar_style_->maximum = 1000;

    green_brush_ = QColor("forestgreen");
    green_back_ = QColor("darkseagreen");

    blue_brush_ = QColor("steelblue");
    blue_back_ = QColor("lightgrey");

    silver_brush_ = QColor("silver");
    silver_back_ = QColor("grey");
}

TorrentDelegate::~TorrentDelegate()
{
    delete progress_bar_style_;
}

/***
****
***/

QSize TorrentDelegate::margin(QStyle const& style) const
{
    Q_UNUSED(style)

    return QSize(4, 4);
}

QString TorrentDelegate::progressString(Torrent const& tor)
{
    bool const is_magnet(!tor.hasMetadata());
    bool const is_done(tor.isDone());
    bool const is_seed(tor.isSeed());
    uint64_t const have_total(tor.haveTotal());
    QString str;
    double seed_ratio;
    bool const has_seed_ratio(tor.getSeedRatio(seed_ratio));

    if (is_magnet) // magnet link with no metadata
    {
        //: First part of torrent progress string;
        //: %1 is the percentage of torrent metadata downloaded
        str = tr("Magnetized transfer - retrieving metadata (%1%)").
            arg(Formatter::percentToString(tor.metadataPercentDone() * 100.0));
    }
    else if (!is_done) // downloading
    {
        //: First part of torrent progress string;
        //: %1 is how much we've got,
        //: %2 is how much we'll have when done,
        //: %3 is a percentage of the two
        str = tr("%1 of %2 (%3%)").arg(Formatter::sizeToString(have_total)).arg(Formatter::sizeToString(tor.sizeWhenDone())).
            arg(Formatter::percentToString(tor.percentDone() * 100.0));
    }
    else if (!is_seed) // partial seed
    {
        if (has_seed_ratio)
        {
            //: First part of torrent progress string;
            //: %1 is how much we've got,
            //: %2 is the torrent's total size,
            //: %3 is a percentage of the two,
            //: %4 is how much we've uploaded,
            //: %5 is our upload-to-download ratio,
            //: %6 is the ratio we want to reach before we stop uploading
            str = tr("%1 of %2 (%3%), uploaded %4 (Ratio: %5 Goal: %6)").arg(Formatter::sizeToString(have_total)).
                arg(Formatter::sizeToString(tor.totalSize())).
                arg(Formatter::percentToString(tor.percentComplete() * 100.0)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio())).
                arg(Formatter::ratioToString(seed_ratio));
        }
        else
        {
            //: First part of torrent progress string;
            //: %1 is how much we've got,
            //: %2 is the torrent's total size,
            //: %3 is a percentage of the two,
            //: %4 is how much we've uploaded,
            //: %5 is our upload-to-download ratio
            str = tr("%1 of %2 (%3%), uploaded %4 (Ratio: %5)").arg(Formatter::sizeToString(have_total)).
                arg(Formatter::sizeToString(tor.totalSize())).
                arg(Formatter::percentToString(tor.percentComplete() * 100.0)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio()));
        }
    }
    else // seeding
    {
        if (has_seed_ratio)
        {
            //: First part of torrent progress string;
            //: %1 is the torrent's total size,
            //: %2 is how much we've uploaded,
            //: %3 is our upload-to-download ratio,
            //: %4 is the ratio we want to reach before we stop uploading
            str = tr("%1, uploaded %2 (Ratio: %3 Goal: %4)").arg(Formatter::sizeToString(have_total)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio())).
                arg(Formatter::ratioToString(seed_ratio));
        }
        else // seeding w/o a ratio
        {
            //: First part of torrent progress string;
            //: %1 is the torrent's total size,
            //: %2 is how much we've uploaded,
            //: %3 is our upload-to-download ratio
            str = tr("%1, uploaded %2 (Ratio: %3)").arg(Formatter::sizeToString(have_total)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio()));
        }
    }

    // add time when downloading
    if ((has_seed_ratio && tor.isSeeding()) || tor.isDownloading())
    {
        if (tor.hasETA())
        {
            //: Second (optional) part of torrent progress string;
            //: %1 is duration;
            //: notice that leading space (before the dash) is included here
            str += tr(" - %1 left").arg(Formatter::timeToString(tor.getETA()));
        }
        else
        {
            //: Second (optional) part of torrent progress string;
            //: notice that leading space (before the dash) is included here
            str += tr(" - Remaining time unknown");
        }
    }

    return str.trimmed();
}

QString TorrentDelegate::shortTransferString(Torrent const& tor)
{
    QString str;
    bool const have_meta(tor.hasMetadata());
    bool const have_down(have_meta && ((tor.webseedsWeAreDownloadingFrom() > 0) || (tor.peersWeAreDownloadingFrom() > 0)));
    bool const have_up(have_meta && tor.peersWeAreUploadingTo() > 0);

    if (have_down)
    {
        str = Formatter::downloadSpeedToString(tor.downloadSpeed()) + QStringLiteral("   ") +
            Formatter::uploadSpeedToString(tor.uploadSpeed());
    }
    else if (have_up)
    {
        str = Formatter::uploadSpeedToString(tor.uploadSpeed());
    }

    return str.trimmed();
}

QString TorrentDelegate::shortStatusString(Torrent const& tor)
{
    QString str;

    switch (tor.getActivity())
    {
    case TR_STATUS_CHECK:
        str = tr("Verifying local data (%1% tested)").arg(Formatter::percentToString(tor.getVerifyProgress() * 100.0));
        break;

    case TR_STATUS_DOWNLOAD:
    case TR_STATUS_SEED:
        str = shortTransferString(tor) + QStringLiteral("    ") + tr("Ratio: %1").arg(Formatter::ratioToString(tor.ratio()));
        break;

    default:
        str = tor.activityString();
        break;
    }

    return str.trimmed();
}

QString TorrentDelegate::statusString(Torrent const& tor)
{
    QString str;

    if (tor.hasError())
    {
        str = tor.getError();
    }
    else
    {
        switch (tor.getActivity())
        {
        case TR_STATUS_STOPPED:
        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK:
        case TR_STATUS_DOWNLOAD_WAIT:
        case TR_STATUS_SEED_WAIT:
            str = shortStatusString(tor);
            break;

        case TR_STATUS_DOWNLOAD:
            if (!tor.hasMetadata())
            {
                str = tr("Downloading metadata from %Ln peer(s) (%1% done)", nullptr, tor.peersWeAreDownloadingFrom()).
                    arg(Formatter::percentToString(100.0 * tor.metadataPercentDone()));
            }
            else
            {
                /* it would be nicer for translation if this was all one string, but I don't see how to do multiple %n's in
                 * tr() */
                if (tor.connectedPeersAndWebseeds() == 0)
                {
                    //: First part of phrase "Downloading from ... peer(s) and ... web seed(s)"
                    str = tr("Downloading from %Ln peer(s)", nullptr, tor.peersWeAreDownloadingFrom());
                }
                else
                {
                    //: First part of phrase "Downloading from ... of ... connected peer(s) and ... web seed(s)"
                    str = tr("Downloading from %1 of %Ln connected peer(s)", nullptr, tor.connectedPeersAndWebseeds()).
                        arg(tor.peersWeAreDownloadingFrom());
                }

                if (tor.webseedsWeAreDownloadingFrom())
                {
                    //: Second (optional) part of phrase "Downloading from ... of ... connected peer(s) and ... web seed(s)";
                    //: notice that leading space (before "and") is included here
                    str += tr(" and %Ln web seed(s)", nullptr, tor.webseedsWeAreDownloadingFrom());
                }
            }

            break;

        case TR_STATUS_SEED:
            if (tor.connectedPeers() == 0)
            {
                str = tr("Seeding to %Ln peer(s)", nullptr, tor.peersWeAreUploadingTo());
            }
            else
            {
                str = tr("Seeding to %1 of %Ln connected peer(s)", nullptr, tor.connectedPeers()).
                    arg(tor.peersWeAreUploadingTo());
            }

            break;

        default:
            str = tr("Error");
            break;
        }
    }

    if (tor.isReadyToTransfer())
    {
        QString s = shortTransferString(tor);

        if (!s.isEmpty())
        {
            str += tr(" - ") + s;
        }
    }

    return str.trimmed();
}

/***
****
***/

QSize TorrentDelegate::sizeHint(QStyleOptionViewItem const& option, Torrent const& tor) const
{
    QSize const m(margin(*qApp->style()));
    ItemLayout const layout(tor.name(), progressString(tor), statusString(tor), QIcon(), option.font, option.direction,
        QPoint(0, 0), option.rect.width() - m.width() * 2);
    return layout.size() + m * 2;
}

QSize TorrentDelegate::sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    // if the font changed, invalidate the height cache
    if (height_font_ != option.font)
    {
        height_font_ = option.font;
        height_hint_.reset();
    }

    // ensure the height is cached
    if (!height_hint_)
    {
        auto const* tor = index.data(TorrentModel::TorrentRole).value<Torrent const*>();
        height_hint_ = sizeHint(option, *tor).height();
    }

    return QSize(option.rect.width(), *height_hint_);
}

QIcon& TorrentDelegate::getWarningEmblem() const
{
    auto& icon = warning_emblem_;

    if (icon.isNull())
    {
        icon = QIcon::fromTheme(QStringLiteral("emblem-important"));
    }

    if (icon.isNull())
    {
        icon = qApp->style()->standardIcon(QStyle::SP_MessageBoxWarning);
    }

    return icon;
}

void TorrentDelegate::paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    auto const* tor(index.data(TorrentModel::TorrentRole).value<Torrent const*>());
    painter->save();
    painter->setClipRect(option.rect);
    drawTorrent(painter, option, *tor);
    painter->restore();
}

void TorrentDelegate::setProgressBarPercentDone(QStyleOptionViewItem const& option, Torrent const& tor) const
{
    double seed_ratio_limit;

    if (tor.isSeeding() && tor.getSeedRatio(seed_ratio_limit))
    {
        double const seed_rate_ratio = tor.ratio() / seed_ratio_limit;
        int const scaled_progress = seed_rate_ratio * (progress_bar_style_->maximum - progress_bar_style_->minimum);
        progress_bar_style_->progress = progress_bar_style_->minimum + scaled_progress;
    }
    else
    {
        bool const is_magnet(!tor.hasMetadata());
        progress_bar_style_->direction = option.direction;
        progress_bar_style_->progress = static_cast<int>(progress_bar_style_->minimum + (is_magnet ? tor.metadataPercentDone() :
            tor.percentDone()) * (progress_bar_style_->maximum - progress_bar_style_->minimum));
    }
}

void TorrentDelegate::drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor) const
{
    QStyle const* style(qApp->style());

    bool const is_paused(tor.isPaused());

    bool const is_item_selected((option.state & QStyle::State_Selected) != 0);
    bool const is_item_enabled((option.state & QStyle::State_Enabled) != 0);
    bool const is_item_active((option.state & QStyle::State_Active) != 0);

    painter->save();

    if (is_item_selected)
    {
        QPalette::ColorGroup cg = is_item_enabled ? QPalette::Normal : QPalette::Disabled;

        if (cg == QPalette::Normal && !is_item_active)
        {
            cg = QPalette::Inactive;
        }

        painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
    }

    QIcon::Mode im;

    if (is_paused || !is_item_enabled)
    {
        im = QIcon::Disabled;
    }
    else if (is_item_selected)
    {
        im = QIcon::Selected;
    }
    else
    {
        im = QIcon::Normal;
    }

    QIcon::State qs;

    if (is_paused)
    {
        qs = QIcon::Off;
    }
    else
    {
        qs = QIcon::On;
    }

    QPalette::ColorGroup cg = QPalette::Normal;

    if (is_paused || !is_item_enabled)
    {
        cg = QPalette::Disabled;
    }

    if (cg == QPalette::Normal && !is_item_active)
    {
        cg = QPalette::Inactive;
    }

    QPalette::ColorRole cr;

    if (is_item_selected)
    {
        cr = QPalette::HighlightedText;
    }
    else
    {
        cr = QPalette::Text;
    }

    QStyle::State progressBarState(option.state);

    if (is_paused)
    {
        progressBarState = QStyle::State_None;
    }

    progressBarState |= QStyle::State_Small;

    QIcon::Mode const emblem_im = is_item_selected ? QIcon::Selected : QIcon::Normal;
    QIcon const emblem_icon = tor.hasError() ? getWarningEmblem() : QIcon();

    // layout
    QSize const m(margin(*style));
    QRect const content_rect(option.rect.adjusted(m.width(), m.height(), -m.width(), -m.height()));
    ItemLayout const layout(tor.name(), progressString(tor), statusString(tor), emblem_icon, option.font, option.direction,
        content_rect.topLeft(), content_rect.width());

    // render
    if (tor.hasError() && !is_item_selected)
    {
        painter->setPen(QColor("red"));
    }
    else
    {
        painter->setPen(option.palette.color(cg, cr));
    }

    tor.getMimeTypeIcon().paint(painter, layout.icon_rect, Qt::AlignCenter, im, qs);

    if (!emblem_icon.isNull())
    {
        emblem_icon.paint(painter, layout.emblem_rect, Qt::AlignCenter, emblem_im, qs);
    }

    painter->setFont(layout.name_font);
    painter->drawText(layout.name_rect, Qt::AlignLeft | Qt::AlignVCenter, layout.nameText());
    painter->setFont(layout.status_font);
    painter->drawText(layout.status_rect, Qt::AlignLeft | Qt::AlignVCenter, layout.statusText());
    painter->setFont(layout.progress_font);
    painter->drawText(layout.progress_rect, Qt::AlignLeft | Qt::AlignVCenter, layout.progressText());
    progress_bar_style_->rect = layout.bar_rect;

    if (tor.isDownloading())
    {
        progress_bar_style_->palette.setBrush(QPalette::Highlight, blue_brush_);
        progress_bar_style_->palette.setColor(QPalette::Base, blue_back_);
        progress_bar_style_->palette.setColor(QPalette::Window, blue_back_);
    }
    else if (tor.isSeeding())
    {
        progress_bar_style_->palette.setBrush(QPalette::Highlight, green_brush_);
        progress_bar_style_->palette.setColor(QPalette::Base, green_back_);
        progress_bar_style_->palette.setColor(QPalette::Window, green_back_);
    }
    else
    {
        progress_bar_style_->palette.setBrush(QPalette::Highlight, silver_brush_);
        progress_bar_style_->palette.setColor(QPalette::Base, silver_back_);
        progress_bar_style_->palette.setColor(QPalette::Window, silver_back_);
    }

    progress_bar_style_->state = progressBarState;
    setProgressBarPercentDone(option, tor);

    style->drawControl(QStyle::CE_ProgressBar, progress_bar_style_, painter);

    painter->restore();
}
