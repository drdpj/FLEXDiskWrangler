# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = guitest
SOURCES += guitest.cpp ../src/mainwindow.cpp ../src/viewer.cpp ../src/hostpolicy.cpp ../src/flextext.cpp ../src/flexdisk.cpp ../src/flexvolume.cpp ../src/uniflexfs.cpp ../src/uniflexvolume.cpp ../src/uniflexwrite.cpp
HEADERS += ../src/mainwindow.h ../src/viewer.h ../src/hostpolicy.h ../src/flextext.h ../src/flexdisk.h ../src/flexvolume.h ../src/uniflexfs.h ../src/uniflexvolume.h ../src/uniflexwrite.h
!include(../core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
