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
// flextext.h - FLEX text files, for the viewer and the tests (QtCore only). FLEX ends a line with CR and stores a
// run of spaces as a TAB ($09) followed by the number of spaces; the end of the last sector is padded with NULs.
#pragma once

#include <QByteArray>
#include <QString>

namespace Flex {

// The text as a host shows it: spaces expanded, CR as a line end (an LF after it, or on its own, ignored), NULs
// dropped. Other control characters are kept.
QString expandText(const QByteArray &data);
// Whether the bytes look like a FLEX text file.
bool looksLikeText(const QByteArray &data);

} // namespace Flex
