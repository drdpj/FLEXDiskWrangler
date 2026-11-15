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
// hostpolicy.h - how FLEX files become host files and back, for the window and flexdw alike (QtCore only). Files go
// out exactly as stored (owner, 15 Nov 2026): a text file keeps its CR line ends and FLEX's space compression, so
// that copying it out and back in gives the same file; the viewer shows text expanded. Host names are the FLEX
// names in lower case unless that clashes.
#pragma once

#include <diskwrangler/xfer/hostread.h>
#include <diskwrangler/xfer/retrofiles.h>

namespace Flex {

class HostPolicy : public dw::xfer::HostPolicy {
public:
    QString hostName(const dw::TransferItem &it) const override;
    bool keepExtension(const dw::TransferItem &) const override { return true; }
};

// The host reader's rules: no file larger than the largest FLEX disc's data area.
dw::xfer::HostReadOptions hostRead();

} // namespace Flex
