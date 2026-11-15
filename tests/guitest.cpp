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

#include <diskwrangler/device/container.h>

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
    CHECK(tools == QStringList({"&Check disc", "Disc &information", "|", "Copy &boot loader from...", "&Link boot loader to file", "|",
                                "Keep a .bak copy when saving"}), "Tools: " + tools.join(", "));

    printf("basic.dsk\n");
    CHECK(w->openFile(dir + "/basic.dsk"), "opened");
    CHECK(w->hasDisc() && w->document()->disk()->files().size() == 5, "five files");
    CHECK(w->windowTitle().startsWith("basic.dsk"), "title: " + w->windowTitle());
    const QString st = w->statusBar()->currentMessage();
    CHECK(st.startsWith("FLEX, 40 tracks of 10 sectors - 5 files") && !st.contains("read-only"), "status: " + st);
    QTreeWidget *t = w->pane();
    QTreeWidgetItem *readme = rowNamed(t, "README.TXT"), *prog = rowNamed(t, "PROG.CMD"), *ran = rowNamed(t, "DATA.RAN");
    CHECK(t->topLevelItem(0) && t->topLevelItem(0)->childCount() == 5 && readme && prog && ran, "a root row with the five files");
    CHECK(t->topLevelItem(0)->text(0).contains("basic.dsk") && t->topLevelItem(0)->text(0).contains("BASIC"), "root: " + t->topLevelItem(0)->text(0));
    if (!readme || !prog || !ran) { printf("%d FAILED\n", fails + 1); return 1; }
    CHECK(prog->text(1) == "WD" && prog->text(3).trimmed() == "12" && prog->text(5) == "1999-12-31", "PROG.CMD: WD, 12 sectors, 1999-12-31");
    CHECK(ran->text(4) == "Random" && ran->text(3).trimmed() == "13", "DATA.RAN: random, 13 sectors");
    CHECK(w->action(Act::AddFiles)->isEnabled() && w->action(Act::Paste)->isEnabled() && w->action(Act::NewImage)->isEnabled(),
          "Add files, Paste and New disc image");

    t->clearSelection();
    readme->setSelected(true);
    t->setCurrentItem(readme);
    w->updateUi();
    CHECK(w->action(Act::View)->isEnabled() && w->action(Act::Extract)->isEnabled() && w->action(Act::Copy)->isEnabled() &&
          w->action(Act::Properties)->isEnabled(), "a file selected: View, Extract, Copy, Properties");
    CHECK(w->action(Act::Rename)->isEnabled() && w->action(Act::Delete)->isEnabled(), "Rename and Delete");

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

    printf("changing a disc (a copy of basic.dsk)\n");
    QTemporaryDir work;
    const QString img = work.path() + "/basic.dsk";
    QFile::copy(dir + "/basic.dsk", img);
    QFile(img).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    auto *w3 = new MainWindow;
    CHECK(w3->openFile(img) && w3->writable(0), "opened, writable");
    QDir().mkpath(work.path() + "/host");
    QFile h1(work.path() + "/host/notes.txt"), h2(work.path() + "/host/empty.txt");
    if (h1.open(QIODevice::WriteOnly)) { h1.write("notes\r"); h1.close(); }
    if (h2.open(QIODevice::WriteOnly)) h2.close();
    w3->importPaths({work.path() + "/host/notes.txt", work.path() + "/host/empty.txt"});
    const Flex::Disk *d3 = w3->document()->disk();
    CHECK(d3->indexOf("NOTES.TXT") >= 0 && d3->indexOf("EMPTY.TXT") < 0 && w3->document()->dirty(), "Add files: NOTES.TXT added, the empty file not");
    CHECK(w3->statusBar()->currentMessage().startsWith("Add files:") || w3->statusBar()->currentMessage().contains("Added 1 file"),
          "the summary: " + w3->statusBar()->currentMessage());
    CHECK(rowNamed(w3->pane(), "NOTES.TXT"), "its row");
    CHECK(w3->renameFile(dw::ui::DiscWindow::Item(0, dw::fs::Path({"NOTES.TXT"})), "memo.txt", &err) && d3->indexOf("MEMO.TXT") >= 0,
          "Rename: NOTES.TXT is MEMO.TXT: " + err);
    CHECK(!w3->renameFile(dw::ui::DiscWindow::Item(0, dw::fs::Path({"PROG.CMD"})), "X.CMD", &err) && err.contains("write-protected"),
          "a protected file isn't renamed: " + err);
    dw::ui::DiscWindow::PropertyChange pc;
    pc.item = dw::ui::DiscWindow::Item(0, dw::fs::Path({"MEMO.TXT"}));
    pc.attrs = {{"write", true}, {"catalog", true}};
    CHECK(w3->applyProperties({pc}, &err) && (d3->files()[d3->indexOf("MEMO.TXT")].attr & 0xF0) == 0x90, "Properties: MEMO.TXT W and C: " + err);
    w3->refresh();                      // what editProperties does after applying its dialog's changes
    CHECK(rowNamed(w3->pane(), "MEMO.TXT") && rowNamed(w3->pane(), "MEMO.TXT")->text(1) == "WC", "the Attr column says WC");
    CHECK(w3->applyVolumeAttrs(0, {{"label", "CHANGED"}, {"number", 12}}, &err) && d3->sir().labelText() == "CHANGED" && d3->sir().volumeNumber == 12,
          "Volume name: CHANGED, 12: " + err);
    CHECK(!w3->deleteFiles({dw::ui::DiscWindow::Item(0, dw::fs::Path({"MEMO.TXT"}))}, &err) && err.contains("write-protected"),
          "a write-protected file isn't deleted: " + err);
    CHECK(w3->deleteFiles({dw::ui::DiscWindow::Item(0, dw::fs::Path({"README.TXT"}))}, &err) && d3->indexOf("README.TXT") < 0, "README.TXT deleted: " + err);
    CHECK(w3->windowTitle().contains("*"), "the title shows the change: " + w3->windowTitle());
    CHECK(w3->save() && !w3->document()->dirty() && !w3->windowTitle().contains("*"), "saved");
    auto *w4 = new MainWindow;
    CHECK(w4->openFile(img) && w4->document()->disk()->indexOf("MEMO.TXT") >= 0 && w4->document()->disk()->sir().labelText() == "CHANGED" &&
          w4->document()->disc().volume(0)->check().ok(), "opened again: the changes, no errors");
    delete w4;

    printf("a new disc, and a boot loader\n");
    // a disc to copy a loader from: one made here, with bytes put in sectors 1 and 2
    const QString src = work.path() + "/system.dsk";
    const dw::fs::Preset *pr = Flex::findPreset("flex-40x10");
    QByteArray loader;
    for (int i = 0; i < 512; ++i) loader.append(char(i * 3 + 1));
    Flex::OpenedDisc sd;
    CHECK(pr && Flex::createBlank(src, *pr, "SYSTEM", 1, QDate(), QByteArray(), &err), "system.dsk made: " + err);
    {
        dw::device::Container c;
        c.open(src, &err);
        const dw::fs::Verdict v0 = Flex::verdict(c, src);
        if (v0.bestCandidate() && sd.open(src, std::any_cast<Flex::ProbeResult>(v0.bestCandidate()->data), &err))
            sd.disc().transact([&](QString *e) { return sd.disk()->setBootSectors(loader, Flex::TS(), e); }, &err) && sd.save(false, &err);
    }
    auto *w5 = new MainWindow;
    const QString made = work.path() + "/new.dsk";
    CHECK(w5->newImage(made, "flex-35x10", "BLANK", 3, src, &err) && w5->hasDisc(), "New disc image: 35 x 10, with system.dsk's loader: " + err);
    if (w5->hasDisc()) {
        const Flex::Disk *nd = w5->document()->disk();
        CHECK(nd->sir().labelText() == "BLANK" && nd->sir().volumeNumber == 3 && nd->files().isEmpty() && nd->hasLoader() && nd->bootLink().isNull() &&
              !w5->document()->dirty(), "opened: BLANK, 3, empty, the loader copied, not linked");
        CHECK(w5->statusBar()->currentMessage().contains("with the boot loader of system.dsk"), "status: " + w5->statusBar()->currentMessage());
        QFile sys(work.path() + "/host/flex.sys");
        if (sys.open(QIODevice::WriteOnly)) { sys.write(QByteArray(2000, 's')); sys.close(); }
        w5->importPaths({sys.fileName()});
        CHECK(w5->linkBootLoader(dw::ui::DiscWindow::Item(0, dw::fs::Path({"FLEX.SYS"})), &err) && nd->bootLink() == Flex::TS(1, 1),
              "Link boot loader to FLEX.SYS: 1/1: " + err);
        QString done;
        CHECK(w5->copyBootLoader(src, &done, &err) && done.contains("linked to FLEX.SYS (1/1)") && nd->bootLink() == Flex::TS(1, 1),
              "Copy boot loader again: linked to FLEX.SYS at once: " + done + err);
    }
    QString e5;
    CHECK(!w5->newImage(work.path() + "/x.dsk", "flex-40x10", "X", 0, dir + "/basic.dsk", &e5) && e5.contains("no boot loader"),
          "a loader from a disc that has none: refused: " + e5);
    delete w5;
    delete w3;

    printf("discs that aren't FLEX\n");
    auto *w2 = new MainWindow;
    CHECK(!w2->openFile(dir + "/os9.dsk") && !w2->hasDisc(), "os9.dsk not opened");
    CHECK(w2->statusBar()->currentMessage().startsWith("Not read as FLEX: it is an OS-9 disc"), "status: " + w2->statusBar()->currentMessage());
    CHECK(!w2->openFile(dir + "/noise.bin") && !w2->hasDisc(), "noise.bin not opened");
    CHECK(w2->statusBar()->currentMessage().startsWith("Not read as FLEX: it isn't a FLEX disc"), "status: " + w2->statusBar()->currentMessage());
    CHECK(w2->openFile(dir + "/bad-chain.dsk") && w2->hasDisc() && w2->readOnlyReason(0) == "the disc has errors (Tools > Check disc)",
          "a damaged disc opens, read-only for its errors");
    CHECK(!w2->action(Act::AddFiles)->isEnabled() && !w2->action(Act::Paste)->isEnabled(), "Add files and Paste greyed on it");
    QTreeWidgetItem *one = rowNamed(w2->pane(), "ONE.TXT");
    CHECK(one && one->toolTip(0).contains("its chain ends"), "the broken file says why: " + (one ? one->toolTip(0) : QString()));
    delete w2;
    delete w;
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
