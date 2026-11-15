# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets
CONFIG += c++17
TARGET = flexdiskwrangler
TEMPLATE = app
win32:RC_ICONS = icons/flexdiskwrangler.ico      # qmake generates the .rc for us
macx {
    ICON = icons/flexdiskwrangler.icns
    QMAKE_TARGET_BUNDLE_PREFIX = io.github.drdpj
}
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    desktop.files = flexdiskwrangler.desktop
    desktop.path = $$PREFIX/share/applications
    svgicon.files = icons/flexdiskwrangler.svg
    svgicon.path = $$PREFIX/share/icons/hicolor/scalable/apps
    INSTALLS += target desktop svgicon
    for(s, $$list(16 24 32 48 64 128 256 512)) {
        icon$${s}.path = $$PREFIX/share/icons/hicolor/$${s}x$${s}/apps
        icon$${s}.extra = $(INSTALL_FILE) $$PWD/icons/flexdiskwrangler-$${s}.png $(INSTALL_ROOT)$$PREFIX/share/icons/hicolor/$${s}x$${s}/apps/flexdiskwrangler.png
        INSTALLS += icon$${s}
    }
}
QMAKE_TARGET_PRODUCT = FLEX Disk Wrangler
QMAKE_TARGET_DESCRIPTION = FLEX Disk Wrangler
QMAKE_TARGET_COPYRIGHT = Copyright (C) 2026 Daniel Jameson. Licensed under the GNU GPL v3 or later.
SOURCES += src/main.cpp src/mainwindow.cpp src/viewer.cpp src/icon.cpp src/hostpolicy.cpp src/flextext.cpp \
           src/flexdisk.cpp src/flexvolume.cpp
HEADERS += src/mainwindow.h src/viewer.h src/icon.h src/hostpolicy.h src/flextext.h \
           src/flexdisk.h src/flexvolume.h
RESOURCES += src/licences.qrc                      # Help > Licences: LICENSE, notices, the LGPL for Qt
exists(licences-bundled.qrc): RESOURCES += licences-bundled.qrc   # a release build's library licences (ci/)
!include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
include(version.pri)                              # APP_VERSION from git describe (Help > About)
