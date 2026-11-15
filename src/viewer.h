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
// viewer.h - View: the core's viewer dialog with FLEX's text view (spaces expanded, CR line ends) in place of the
// plain one, and the hex dump.
#pragma once

#include <diskwrangler/ui/viewerdialog.h>

class ViewerDialog : public dw::ViewerDialog {
    Q_OBJECT
public:
    ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent = nullptr);
};
