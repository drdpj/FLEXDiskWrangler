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
// guitest - FLEX Disk Wrangler's window on the kit: opening (a FLEX disc, an OS-9 disc it can't open, noise), the
// menus in the family's order, what is enabled on a disc that is only read, the rows and their cells, View with
// FLEX's text view, Extract (the files as stored, named in lower case, dated), Copy, Properties and Disc information.
//   guitest <dir made by flex_check.py make>        (QT_QPA_PLATFORM=offscreen)
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenuBar>
#include <QMimeData>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <cstdio>

#include <diskwrangler/app/appsettings.h>
#include <diskwrangler/xfer/mime.h>
#include "../src/mainwindow.h"
#include "../src/viewer.h"

using Act = dw::ui::DiscWindow::Act;

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, qPrintable(QString(what))); ++fails; } \
                            else printf("  ok: %s\n", qPrintable(QString(what))); } while (0)

static QStringList menuTexts(QMenu *m) {
    QStringList l;
    for (QAction *a : m->actions()) l << (a->isSeparator() ? QString("|") : a->text());
    return l;
}

static QMenu *menu(MainWindow *w, const QString &title) {
    for (QAction *a : w->menuBar()->actions()) if (a->text() == title) return a->menu();
    return nullptr;
}

static QTreeWidgetItem *rowNamed(QTreeWidget *t, const QString &name) {
    QTreeWidgetItem *root = t->topLevelItem(0);
    for (int i = 0; root && i < root->childCount(); ++i) if (root->child(i)->text(0) == name) return root->child(i);
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationVersion("test");
    if (argc < 2) { fprintf(stderr, "usage: guitest <dir made by flex_check.py make>\n"); return 2; }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    dw::ui::DiscWindow::autoAnswer = true;

    printf("an empty window\n");
    auto *w = new MainWindow;
    w->show();
    CHECK(w->windowTitle().contains("FLEX Disk Wrangler"), "title: " + w->windowTitle());
    CHECK(w->statusBar()->currentMessage().startsWith("Open a FLEX disc image"), "status: " + w->statusBar()->currentMessage());
    CHECK(!w->action(Act::View)->isEnabled() && !w->action(Act::Extract)->isEnabled() && w->action(Act::Open)->isEnabled(), "nothing to view or extract");

    printf("the menus, in the family's order\n");
    const QStringList file = menuTexts(menu(w, "&File")), edit = menuTexts(menu(w, "&Edit")), tools = menuTexts(menu(w, "&Tools"));
    QStringList wantFile = {"New &window", "&New disc image...", "&Open...", "|", "&Save", "Save &as...", "&Revert", "|"};
    if (AppSettings::offered()) wantFile << "Create &local config file..." << "|";      // only where portable settings exist
    wantFile << "&Close" << "&Quit";
    CHECK(file == wantFile, "File: " + file.join(", "));
    CHECK(edit == QStringList({"&View (text / hex)", "E&xtract to folder...", "|", "&Add files...", "|", "&Copy", "&Paste", "|",
                               "Re&name...", "P&roperties...", "&Delete", "|", "Volume &name..."}), "Edit: " + edit.join(", "));
    CHECK(tools.first() == "&Check disc" && tools.last() == "Keep a .bak copy when saving", "Tools: Check disc first, the .bak option last: " + tools.join(", "));

    printf("basic.dsk\n");
    CHECK(w->openFile(dir + "/basic.dsk"), "opened");
    CHECK(w->hasDisc() && w->document()->disk()->files().size() == 5, "five files");
    CHECK(w->windowTitle().startsWith("basic.dsk"), "title: " + w->windowTitle());
    const QString st = w->statusBar()->currentMessage();
    CHECK(st.startsWith("FLEX, 40 tracks of 10 sectors - 5 files") && st.contains("read-only: " + QString(Flex::FlexVolume::ReadOnlyText)), "status: " + st);
    QTreeWidget *t = w->pane();
    QTreeWidgetItem *readme = rowNamed(t, "README.TXT"), *prog = rowNamed(t, "PROG.CMD"), *ran = rowNamed(t, "DATA.RAN");
    CHECK(t->topLevelItem(0) && t->topLevelItem(0)->childCount() == 5 && readme && prog && ran, "a root row with the five files");
    CHECK(t->topLevelItem(0)->text(0).contains("basic.dsk") && t->topLevelItem(0)->text(0).contains("BASIC"), "root: " + t->topLevelItem(0)->text(0));
    if (!readme || !prog || !ran) { printf("%d FAILED\n", fails + 1); return 1; }
    CHECK(prog->text(1) == "WD" && prog->text(3).trimmed() == "12" && prog->text(5) == "1999-12-31", "PROG.CMD: WD, 12 sectors, 1999-12-31");
    CHECK(ran->text(4) == "Random" && ran->text(3).trimmed() == "13", "DATA.RAN: random, 13 sectors");
    CHECK(!w->action(Act::AddFiles)->isEnabled() && !w->action(Act::Paste)->isEnabled() && !w->action(Act::NewImage)->isEnabled(),
          "Add files, Paste and New disc image greyed");

    t->clearSelection();
    readme->setSelected(true);
    t->setCurrentItem(readme);
    w->updateUi();
    CHECK(w->action(Act::View)->isEnabled() && w->action(Act::Extract)->isEnabled() && w->action(Act::Copy)->isEnabled() &&
          w->action(Act::Properties)->isEnabled(), "a file selected: View, Extract, Copy, Properties");
    CHECK(!w->action(Act::Rename)->isEnabled() && !w->action(Act::Delete)->isEnabled(), "Rename and Delete greyed (read-only)");

    printf("View\n");
    w->viewSelected();
    ViewerDialog *v = w->findChild<ViewerDialog *>();
    CHECK(v && v->viewId() == "flextext" && v->shownText().startsWith("* A FLEX text file\n        LDA     #$41"), "FLEX text, spaces expanded: " + (v ? v->viewId() : QString("no dialog")));
    if (v) v->close();

    printf("Extract\n");
    QTemporaryDir out;
    int files = 0;
    QStringList errors;
    QJsonArray json;
    QString err;
    QVector<dw::ui::DiscWindow::Item> all;
    for (const Flex::File &f : w->document()->disk()->files()) all << dw::ui::DiscWindow::Item(0, dw::fs::Path({f.name}));
    CHECK(w->extractTo(all, out.path(), &files, &errors, &json, &err) && files == 5 && errors.isEmpty(), "five files extracted: " + err + errors.join("; "));
    const QStringList got = QDir(out.path()).entryList(QDir::Files, QDir::Name);
    CHECK(got == QStringList({"data.ran", "empty", "hidden.sys", "prog.cmd", "readme.txt"}), "names in lower case: " + got.join(" "));
    QByteArray disc;
    w->document()->disc().volume(0)->read(dw::fs::Path({"README.TXT"}), &disc, &err);
    QFile f(out.path() + "/readme.txt");
    CHECK(f.open(QIODevice::ReadOnly) && f.readAll() == disc, "README.TXT exactly as stored");
    CHECK(QFileInfo(out.path() + "/prog.cmd").lastModified().date() == QDate(1999, 12, 31), "dated as on the disc");

    printf("Copy, Properties, Disc information, Check\n");
    w->copySelected();
    const QMimeData *m = QApplication::clipboard()->mimeData();
    CHECK(m && m->hasFormat(DW_MIME_CLIP), "the clipboard holds the copy");
    w->editProperties();
    CHECK(w->statusBar()->currentMessage() == "properties shown", "Properties shown, nothing changed");
    QMetaObject::invokeMethod(w, "discInfo");
    CHECK(w->statusBar()->currentMessage() == "info shown", "Disc information shown");
    w->checkDisc();
    CHECK(w->statusBar()->currentMessage().contains("No problems found"), "Check: " + w->statusBar()->currentMessage());
    CHECK(!w->document()->dirty(), "nothing changed");

    printf("discs that aren't FLEX\n");
    auto *w2 = new MainWindow;
    CHECK(!w2->openFile(dir + "/os9.dsk") && !w2->hasDisc(), "os9.dsk not opened");
    CHECK(w2->statusBar()->currentMessage().startsWith("Not read as FLEX: it is an OS-9 disc"), "status: " + w2->statusBar()->currentMessage());
    CHECK(!w2->openFile(dir + "/noise.bin") && !w2->hasDisc(), "noise.bin not opened");
    CHECK(w2->statusBar()->currentMessage().startsWith("Not read as FLEX: it isn't a FLEX disc"), "status: " + w2->statusBar()->currentMessage());
    CHECK(w2->openFile(dir + "/bad-chain.dsk") && w2->hasDisc() && w2->readOnlyReason(0) == "the disc has errors (Tools > Check disc)",
          "a damaged disc opens, read-only for its errors");
    QTreeWidgetItem *one = rowNamed(w2->pane(), "ONE.TXT");
    CHECK(one && one->toolTip(0).contains("its chain ends"), "the broken file says why: " + (one ? one->toolTip(0) : QString()));
    delete w2;
    delete w;
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
