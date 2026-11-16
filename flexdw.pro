# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
# flexdw: the command-line program (QtCore only: no window, no display needed). Build it beside the GUI program:
#     mkdir b && cd b && qmake ../flexdw.pro && make
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = flexdw
TEMPLATE = app
win32 {                                            # version-info strings in the .exe (Explorer: file Properties > Details)
    QMAKE_TARGET_PRODUCT = FLEX Disk Wrangler
    QMAKE_TARGET_DESCRIPTION = flexdw, the command-line program of FLEX Disk Wrangler
    QMAKE_TARGET_COPYRIGHT = Copyright (C) 2026 Daniel Jameson. Licensed under the GNU GPL v3 or later.
}
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    INSTALLS += target
}
SOURCES += src/cli/main.cpp src/cli/backend.cpp src/flexdisk.cpp src/flexvolume.cpp src/uniflexfs.cpp src/uniflexvolume.cpp src/hostpolicy.cpp
HEADERS += src/cli/backend.h src/flexdisk.h src/flexvolume.h src/uniflexfs.h src/uniflexvolume.h src/hostpolicy.h
!exists(core/diskwrangler/cli/commands.cpp): error("core/ is empty or too old: run 'git submodule update --init'")
INCLUDEPATH += core
include(core/diskwrangler-cli.pri)
include(version.pri)
