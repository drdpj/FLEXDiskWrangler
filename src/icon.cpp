/*
 * FLEX Disk Wrangler - a tool for FLEX disc images
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "icon.h"

#include <QPainter>
#include <QPainterPath>

// Laid out on a 100 x 100 grid. A 5.25-inch disc (the size most FLEX systems used): a square black sleeve with a
// hub ring, the index hole, an oval head slot below the hub, a write-protect notch on the right edge, and a label
// with an orange band (the 6809's era) and ruled lines.
QImage floppyIconImage(int size) {
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 100.0, size / 100.0);
    p.setPen(Qt::NoPen);
    const QColor liner("#4a4a52"), dark("#141418");

    QPainterPath sleeve;
    sleeve.addRoundedRect(QRectF(5, 5, 90, 90), 2.5, 2.5);
    QPainterPath notch;
    notch.addRect(QRectF(91.5, 20, 3.6, 8));
    p.setBrush(QColor("#26262c"));
    p.drawPath(sleeve.subtracted(notch));

    // label
    p.setBrush(QColor("#f4f1e8"));
    p.drawRoundedRect(QRectF(12, 10, 56, 19), 1.6, 1.6);
    p.setBrush(QColor("#d8762a"));
    p.drawRect(QRectF(12, 10, 56, 5.5));
    p.setBrush(QColor("#8c8779"));
    p.drawRect(QRectF(16, 19.5, 40, 1.5));
    p.drawRect(QRectF(16, 24, 30, 1.5));

    // hub: a reinforcing ring around a smaller opening than an 8" disc's
    const QPointF c(50, 52);
    p.setBrush(liner);
    p.drawEllipse(c, 12.5, 12.5);
    p.setBrush(QColor("#8a7c6a"));                       // the hub ring
    p.drawEllipse(c, 10.5, 10.5);
    p.setBrush(dark);
    p.drawEllipse(c, 6.5, 6.5);
    p.setBrush(liner);                                   // index hole
    p.drawEllipse(QPointF(c.x() + 17, c.y() - 4), 2.0, 2.0);

    // head slot
    p.setBrush(liner);
    p.drawRoundedRect(QRectF(45.5, 69, 9, 23), 4.5, 4.5);
    return img;
}

QIcon appIcon() {
    QIcon ic;
    const int sizes[] = {16, 20, 24, 32, 48, 64, 128, 256};
    for (int s : sizes) ic.addPixmap(QPixmap::fromImage(floppyIconImage(s)));
    return ic;
}
