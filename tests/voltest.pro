# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT -= gui
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = voltest
SOURCES += voltest.cpp ../src/flexdisk.cpp ../src/flexvolume.cpp ../src/flextext.cpp
HEADERS += ../src/flexdisk.h ../src/flexvolume.h ../src/flextext.h
!include(../core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
