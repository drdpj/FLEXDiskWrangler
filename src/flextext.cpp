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
#include "flextext.h"

namespace Flex {

QString expandText(const QByteArray &d) {
    QString s;
    s.reserve(d.size());
    for (int i = 0; i < d.size(); ++i) {
        const uchar c = uchar(d[i]);
        if (c == 0x09 && i + 1 < d.size()) { s += QString(uchar(d[++i]), QChar(' ')); continue; }
        if (c == 0x0D) { s += '\n'; continue; }
        if (c == 0x0A || c == 0x00) continue;
        s += QChar(c);
    }
    return s;
}

bool looksLikeText(const QByteArray &d) {
    int text = 0, other = 0, crs = 0;
    for (int i = 0; i < d.size(); ++i) {
        const uchar c = uchar(d[i]);
        if (c == 0x09 && i + 1 < d.size()) { ++i; ++text; continue; }
        if (c == 0x0D) { ++crs; ++text; continue; }
        if (c == 0x00) continue;
        if ((c >= 0x20 && c < 0x7F) || c == 0x0A || c == 0x0C) ++text;
        else ++other;
    }
    return text > 0 && crs > 0 && other * 50 <= text;
}

} // namespace Flex
