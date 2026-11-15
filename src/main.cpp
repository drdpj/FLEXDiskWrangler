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
#include <QApplication>

#include <diskwrangler/ui/startup.h>

#include "icon.h"
#include "mainwindow.h"

#ifndef APP_VERSION                // set by version.pri / version.cmake from git describe
#define APP_VERSION "unknown"
#endif

// The start-up every Disk Wrangler shares is the window kit's (diskwrangler/ui/startup.h).
int main(int argc, char *argv[]) {
    dw::ui::beforeApplication("flexdiskwrangler");
    QApplication app(argc, argv);
    dw::ui::setUpApplication("flexdiskwrangler", APP_VERSION, appIcon());
    return dw::ui::runWindows<MainWindow>(app, argc, argv);
}
