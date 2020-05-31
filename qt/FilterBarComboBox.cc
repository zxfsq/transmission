/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QApplication>
#include <QStyle>
#include <QStylePainter>

#include "FilterBarComboBox.h"
#include "StyleHelper.h"
#include "Utils.h"

namespace
{

int getHSpacing(QWidget const* w)
{
    return qMax(3, w->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, w));
}

} // namespace

FilterBarComboBox::FilterBarComboBox(QWidget* parent) :
    QComboBox(parent)
{
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

QSize FilterBarComboBox::minimumSizeHint() const
{
    QFontMetrics fm(fontMetrics());
    QSize const text_size = fm.boundingRect(itemText(0)).size();
    QSize const count_size = fm.boundingRect(itemData(0, CountStringRole).toString()).size();
    return calculateSize(text_size, count_size);
}

QSize FilterBarComboBox::sizeHint() const
{
    QFontMetrics fm(fontMetrics());
    QSize max_text_size(0, 0);
    QSize max_count_size(0, 0);

    for (int i = 0, n = count(); i < n; ++i)
    {
        QSize const text_size = fm.boundingRect(itemText(i)).size();
        max_text_size.setHeight(qMax(max_text_size.height(), text_size.height()));
        max_text_size.setWidth(qMax(max_text_size.width(), text_size.width()));

        QSize const count_size = fm.boundingRect(itemData(i, CountStringRole).toString()).size();
        max_count_size.setHeight(qMax(max_count_size.height(), count_size.height()));
        max_count_size.setWidth(qMax(max_count_size.width(), count_size.width()));
    }

    return calculateSize(max_text_size, max_count_size);
}

QSize FilterBarComboBox::calculateSize(QSize const& text_size, QSize const& count_size) const
{
    int const hmargin = getHSpacing(this);

    QStyleOptionComboBox option;
    initStyleOption(&option);

    QSize content_size = iconSize() + QSize(4, 2);
    content_size.setHeight(qMax(content_size.height(), text_size.height()));
    content_size.rwidth() += hmargin + text_size.width();
    content_size.rwidth() += hmargin + count_size.width();

    return style()->sizeFromContents(QStyle::CT_ComboBox, &option, content_size, this).expandedTo(qApp->globalStrut());
}

void FilterBarComboBox::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e)

    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));

    // draw the combobox frame, focusrect and selected etc.
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // draw the icon and text
    QModelIndex const model_index = model()->index(currentIndex(), 0, rootModelIndex());

    if (model_index.isValid())
    {
        QStyle* s = style();
        int const hmargin = getHSpacing(this);

        QRect rect = s->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        rect.adjust(2, 1, -2, -1);

        // draw the icon
        QIcon const icon = Utils::getIconFromIndex(model_index);

        if (!icon.isNull())
        {
            QRect const icon_rect = QStyle::alignedRect(opt.direction, Qt::AlignLeft | Qt::AlignVCenter, opt.iconSize, rect);
            icon.paint(&painter, icon_rect, Qt::AlignCenter, StyleHelper::getIconMode(opt.state), QIcon::Off);
            Utils::narrowRect(rect, icon_rect.width() + hmargin, 0, opt.direction);
        }

        // draw the count
        QString text = model_index.data(CountStringRole).toString();

        if (!text.isEmpty())
        {
            QPen const pen = painter.pen();
            painter.setPen(Utils::getFadedColor(pen.color()));
            QRect const text_rect = QStyle::alignedRect(opt.direction, Qt::AlignRight | Qt::AlignVCenter,
                QSize(opt.fontMetrics.boundingRect(text).width(), rect.height()), rect);
            painter.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, text);
            Utils::narrowRect(rect, 0, text_rect.width() + hmargin, opt.direction);
            painter.setPen(pen);
        }

        // draw the text
        text = model_index.data(Qt::DisplayRole).toString();
        text = painter.fontMetrics().elidedText(text, Qt::ElideRight, rect.width());
        painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}
