/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QTextDocument>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "FaviconCache.h"
#include "Formatter.h"
#include "Torrent.h"
#include "TrackerDelegate.h"
#include "TrackerModel.h"
#include "Utils.h"

/***
****
***/

namespace
{

int const SPACING = 6;
QSize const myMargin(10, 10);

class ItemLayout
{
private:
    QTextDocument text_document_;

public:
    QRect icon_rect;
    QRect text_rect;

    ItemLayout(QString const& text, bool suppress_colors, Qt::LayoutDirection direction, QPoint const& top_left, int width);

    QSize size() const
    {
        return (icon_rect | text_rect).size();
    }

    QAbstractTextDocumentLayout* textLayout() const
    {
        return text_document_.documentLayout();
    }
};

ItemLayout::ItemLayout(QString const& text, bool suppress_colors, Qt::LayoutDirection direction, QPoint const& top_left,
    int width)
{
    QStyle const* style(qApp->style());
    QSize const icon_size = FaviconCache::getIconSize();

    QRect base_rect(top_left, QSize(width, 0));

    icon_rect = style->alignedRect(direction, Qt::AlignLeft | Qt::AlignTop, icon_size, base_rect);
    Utils::narrowRect(base_rect, icon_size.width() + SPACING, 0, direction);

    text_document_.setDocumentMargin(0);
    text_document_.setTextWidth(base_rect.width());

    QTextOption text_option;
    text_option.setTextDirection(direction);

    if (suppress_colors)
    {
        text_option.setFlags(QTextOption::SuppressColors);
    }

    text_document_.setDefaultTextOption(text_option);
    text_document_.setHtml(text);

    text_rect = base_rect;
    text_rect.setSize(text_document_.size().toSize());
}

} // namespace

/***
****
***/

QSize TrackerDelegate::sizeHint(QStyleOptionViewItem const& option, TrackerInfo const& info) const
{
    ItemLayout const layout(getText(info), true, option.direction, QPoint(0, 0), option.rect.width() - myMargin.width() * 2);
    return layout.size() + myMargin * 2;
}

QSize TrackerDelegate::sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    auto const tracker_info = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
    return sizeHint(option, tracker_info);
}

void TrackerDelegate::paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    auto const tracker_info = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
    painter->save();
    painter->setClipRect(option.rect);
    drawBackground(painter, option, index);
    drawTracker(painter, option, tracker_info);
    drawFocus(painter, option, option.rect);
    painter->restore();
}

void TrackerDelegate::drawTracker(QPainter* painter, QStyleOptionViewItem const& option, TrackerInfo const& inf) const
{
    bool const is_item_selected((option.state & QStyle::State_Selected) != 0);
    bool const is_item_enabled((option.state & QStyle::State_Enabled) != 0);
    bool const is_item_active((option.state & QStyle::State_Active) != 0);

    QIcon trackerIcon(inf.st.getFavicon());

    QRect const content_rect(option.rect.adjusted(myMargin.width(), myMargin.height(), -myMargin.width(), -myMargin.height()));
    ItemLayout const layout(getText(inf), is_item_selected, option.direction, content_rect.topLeft(), content_rect.width());

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

    trackerIcon.paint(painter, layout.icon_rect, Qt::AlignCenter, is_item_selected ? QIcon::Selected : QIcon::Normal,
        QIcon::On);

    QAbstractTextDocumentLayout::PaintContext paint_context;
    paint_context.clip = layout.text_rect.translated(-layout.text_rect.topLeft());
    paint_context.palette.setColor(QPalette::Text,
        option.palette.color(is_item_selected ? QPalette::HighlightedText : QPalette::Text));
    painter->translate(layout.text_rect.topLeft());
    layout.textLayout()->draw(painter, paint_context);

    painter->restore();
}

void TrackerDelegate::setShowMore(bool b)
{
    show_more_ = b;
}

namespace
{

QString timeToStringRounded(int seconds)
{
    if (seconds > 60)
    {
        seconds -= seconds % 60;
    }

    return Formatter::timeToString(seconds);
}

} // namespace

QString TrackerDelegate::getText(TrackerInfo const& inf) const
{
    QString key;
    QString str;
    time_t const now(time(nullptr));
    auto const err_markup_begin = QStringLiteral("<span style=\"color:red\">");
    auto const err_markup_end = QStringLiteral("</span>");
    auto const timeout_markup_begin = QStringLiteral("<span style=\"color:#224466\">");
    auto const timeout_markup_end = QStringLiteral("</span>");
    auto const success_markup_begin = QStringLiteral("<span style=\"color:#008B00\">");
    auto const success_markup_end = QStringLiteral("</span>");

    // hostname
    str += inf.st.is_backup ? QStringLiteral("<i>") : QStringLiteral("<b>");
    char* host = nullptr;
    int port = 0;
    tr_urlParse(inf.st.announce.toUtf8().constData(), TR_BAD_SIZE, nullptr, &host, &port, nullptr);
    str += QStringLiteral("%1:%2").arg(QString::fromUtf8(host)).arg(port);
    tr_free(host);

    if (!key.isEmpty())
    {
        str += QStringLiteral(" - ") + key;
    }

    str += inf.st.is_backup ? QStringLiteral("</i>") : QStringLiteral("</b>");

    // announce & scrape info
    if (!inf.st.is_backup)
    {
        if (inf.st.has_announced && inf.st.announce_state != TR_TRACKER_INACTIVE)
        {
            QString const tstr(timeToStringRounded(now - inf.st.last_announce_time));
            str += QStringLiteral("<br/>\n");

            if (inf.st.last_announce_succeeded)
            {
                //: %1 and %2 are replaced with HTML markup, %3 is duration
                str += tr("Got a list of%1 %Ln peer(s)%2 %3 ago", nullptr, inf.st.last_announce_peer_count).
                    arg(success_markup_begin).arg(success_markup_end).arg(tstr);
            }
            else if (inf.st.last_announce_timed_out)
            {
                //: %1 and %2 are replaced with HTML markup, %3 is duration
                str += tr("Peer list request %1timed out%2 %3 ago; will retry").arg(timeout_markup_begin).
                    arg(timeout_markup_end).arg(tstr);
            }
            else
            {
                //: %1 and %3 are replaced with HTML markup, %2 is error message, %4 is duration
                str += tr("Got an error %1\"%2\"%3 %4 ago").arg(err_markup_begin).arg(inf.st.last_announce_result).
                    arg(err_markup_end).arg(tstr);
            }
        }

        switch (inf.st.announce_state)
        {
        case TR_TRACKER_INACTIVE:
            str += QStringLiteral("<br/>\n");
            str += tr("No updates scheduled");
            break;

        case TR_TRACKER_WAITING:
            {
                QString const tstr(timeToStringRounded(inf.st.next_announce_time - now));
                str += QStringLiteral("<br/>\n");
                //: %1 is duration
                str += tr("Asking for more peers in %1").arg(tstr);
                break;
            }

        case TR_TRACKER_QUEUED:
            str += QStringLiteral("<br/>\n");
            str += tr("Queued to ask for more peers");
            break;

        case TR_TRACKER_ACTIVE:
            {
                QString const tstr(timeToStringRounded(now - inf.st.last_announce_start_time));
                str += QStringLiteral("<br/>\n");
                //: %1 is duration
                str += tr("Asking for more peers now... <small>%1</small>").arg(tstr);
                break;
            }
        }

        if (show_more_)
        {
            if (inf.st.has_scraped)
            {
                str += QStringLiteral("<br/>\n");
                QString const tstr(timeToStringRounded(now - inf.st.last_scrape_time));

                if (inf.st.last_scrape_succeeded)
                {
                    if (inf.st.seeder_count >= 0 && inf.st.leecher_count >= 0)
                    {
                        //: First part of phrase "Tracker had ... seeder(s) and ... leecher(s) ... ago";
                        //: %1 and %2 are replaced with HTML markup
                        str += tr("Tracker had%1 %Ln seeder(s)%2", nullptr, inf.st.seeder_count).arg(success_markup_begin).
                            arg(success_markup_end);
                        //: Second part of phrase "Tracker had ... seeder(s) and ... leecher(s) ... ago";
                        //: %1 and %2 are replaced with HTML markup, %3 is duration;
                        //: notice that leading space (before "and") is included here
                        str += tr(" and%1 %Ln leecher(s)%2 %3 ago", nullptr, inf.st.leecher_count).arg(success_markup_begin).
                            arg(success_markup_end).arg(tstr);
                    }
                    else
                    {
                        //: %1 and %2 are replaced with HTML markup, %3 is duration
                        str += tr("Tracker had %1no information%2 on peer counts %3 ago").arg(success_markup_begin).
                            arg(success_markup_end).arg(tstr);
                    }
                }
                else
                {
                    //: %1 and %3 are replaced with HTML markup, %2 is error message, %4 is duration
                    str += tr("Got a scrape error %1\"%2\"%3 %4 ago").arg(err_markup_begin).arg(inf.st.last_scrape_result).
                        arg(err_markup_end).arg(tstr);
                }
            }

            switch (inf.st.scrape_state)
            {
            case TR_TRACKER_INACTIVE:
                break;

            case TR_TRACKER_WAITING:
                {
                    str += QStringLiteral("<br/>\n");
                    QString const tstr(timeToStringRounded(inf.st.next_scrape_time - now));
                    //: %1 is duration
                    str += tr("Asking for peer counts in %1").arg(tstr);
                    break;
                }

            case TR_TRACKER_QUEUED:
                {
                    str += QStringLiteral("<br/>\n");
                    str += tr("Queued to ask for peer counts");
                    break;
                }

            case TR_TRACKER_ACTIVE:
                {
                    str += QStringLiteral("<br/>\n");
                    QString const tstr(timeToStringRounded(now - inf.st.last_scrape_start_time));
                    //: %1 is duration
                    str += tr("Asking for peer counts now... <small>%1</small>").arg(tstr);
                    break;
                }
            }
        }
    }

    return str;
}
