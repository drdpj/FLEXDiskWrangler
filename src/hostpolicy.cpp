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
#include "hostpolicy.h"

namespace Flex {

QString HostPolicy::hostName(const dw::TransferItem &it) const {
    dw::TransferItem t = it;
    if (t.hostName.isEmpty() && t.name == t.name.toUpper()) t.hostName = t.name.toLower();
    return dw::xfer::HostPolicy::hostName(t);
}

dw::xfer::HostReadOptions hostRead() {
    dw::xfer::HostReadOptions o;
    o.sizeLimit = qint64(256) * 255 * 252;     // 256 tracks of 255 sectors: more than any FLEX disc holds
    return o;
}

} // namespace Flex
