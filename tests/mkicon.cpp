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
// mkicon <dir>: writes the application icon as flexdiskwrangler-<size>.png for the usual sizes (icons/ holds them;
// the .ico and .icns are made from them with Pillow, docs/testing.md).
#include <QGuiApplication>
#include "../src/icon.h"
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const QString dir = argc > 1 ? argv[1] : ".";
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512}) floppyIconImage(s).save(QString("%1/flexdiskwrangler-%2.png").arg(dir).arg(s));
    return 0;
}
