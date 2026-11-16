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
// guitest - FLEX Disk Wrangler's window on the kit: opening (a FLEX disc, a UniFLEX disc, an OS-9 disc it can't open,
// noise), the menus in the family's order, what is enabled, the rows and their cells, View with FLEX's and UniFLEX's
// text views, Extract (the files as stored, FLEX names in lower case, UniFLEX names as they are, dated), Copy,
// Properties and Disc information, and the changes FLEX Disk Wrangler makes to a FLEX disc.
//   guitest <dir made by flex_check.py make and uniflex_check.py make>        (QT_QPA_PLATFORM=offscreen)
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenuBar>
#include <QMimeData>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTabWidget>
#include <QTimeZone>
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

static QTreeWidgetItem *childNamed(QTreeWidgetItem *p, const QString &name) {
    for (int i = 0; p && i < p->childCount(); ++i) if (p->child(i)->text(0) == name) return p->child(i);
    return nullptr;
}

// The view menu's entries: one "Text" each, never FLEX's and UniFLEX's both
static QStringList viewMenu(QWidget *viewer) {
    QStringList l;
    if (QComboBox *c = viewer ? viewer->findChild<QComboBox *>() : nullptr) for (int i = 0; i < c->count(); ++i) l << c->itemText(i);
    return l;
}

static QAction *menuAction(MainWindow *w, const QString &menuTitle, const QString &text) {
    QMenu *m = menu(w, menuTitle);
    for (QAction *a : m ? m->actions() : QList<QAction *>()) if (a->text() == text) return a;
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
    CHECK(w->statusBar()->currentMessage().startsWith("Open a FLEX or UniFLEX disc image"), "status: " + w->statusBar()->currentMessage());
    CHECK(!w->action(Act::View)->isEnabled() && !w->action(Act::Extract)->isEnabled() && w->action(Act::Open)->isEnabled(), "nothing to view or extract");

    printf("the menus, in the family's order\n");
    const QStringList file = menuTexts(menu(w, "&File")), edit = menuTexts(menu(w, "&Edit")), tools = menuTexts(menu(w, "&Tools"));
    QStringList wantFile = {"New &window", "&New disc image...", "&Open...", "|", "&Save", "Save &as...", "&Revert", "|"};
    if (AppSettings::offered()) wantFile << "Create &local config file..." << "|";      // only where portable settings exist
    wantFile << "&Close" << "&Quit";
    CHECK(file == wantFile, "File: " + file.join(", "));
    CHECK(edit == QStringList({"&View (text / hex)", "E&xtract to folder...", "|", "&Add files...", "New &directory...", "|", "&Copy", "&Paste", "|",
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
    CHECK(viewMenu(v).count("Text") == 1, "one Text in the view menu: " + viewMenu(v).join(", "));
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

    printf("a UniFLEX disc (uf-basic.dsk): a tree, changed as UniFLEX changes it\n");
    auto *w6 = new MainWindow;
    w6->show();
    QFile::copy(dir + "/uf-basic.dsk", work.path() + "/uf-basic.dsk");
    CHECK(w6->openFile(work.path() + "/uf-basic.dsk") && w6->hasUniflex() && !w6->hasFlex(), "opened as UniFLEX (a copy)");
    dw::ui::VolumePane *up = w6->pane("uniflex");
    CHECK(up && w6->tabWidget()->currentWidget() == up && w6->tabWidget()->count() == 1 && !w6->tabWidget()->tabBar()->isVisible(),
          "the UniFLEX pane, alone, no tabs");
    const QString ust = w6->statusBar()->currentMessage();
    CHECK(ust.startsWith("UniFLEX, 600 blocks - 12 files in 5 directories") && !ust.contains("read-only"), "status: " + ust);
    QTreeWidgetItem *uroot = up ? up->topLevelItem(0) : nullptr;
    CHECK(uroot && uroot->text(0).contains("uf-basic.dsk") && uroot->text(0).contains("basic"), "root: " + (uroot ? uroot->text(0) : QString()));
    QTreeWidgetItem *usrc = childNamed(uroot, "src"), *ureadme = childNamed(uroot, "readme"), *uprog = childNamed(uroot, "Prog");
    QTreeWidgetItem *sub = childNamed(usrc, "sub"), *tty = childNamed(childNamed(uroot, "dev"), "tty0");
    CHECK(usrc && ureadme && uprog && sub && childNamed(sub, "deep.c") && tty, "rows for the tree: src/sub/deep.c, dev/tty0, readme, Prog");
    if (ureadme && uprog && tty) {
        CHECK(ureadme->text(1) == "rw-rw-" && ureadme->text(4).trimmed() == "49" && !ureadme->text(5).isEmpty(), "readme: rw-rw-, 49 bytes, a time");
        CHECK(uprog->text(1) == "rwxr-x" && uprog->text(2).trimmed() == "3", "Prog: rwxr-x, owner 3");
        CHECK(tty->text(4) == "character device 3, 1", "dev/tty0: " + tty->text(4));
    }
    CHECK(w6->action(Act::AddFiles)->isEnabled() && w6->action(Act::Paste)->isEnabled() && w6->action(Act::NewDirectory)->isEnabled() &&
          w6->action(Act::NewImage)->isEnabled(), "Add files, Paste and New directory there; New disc image (FLEX) too");
    QAction *volName = menuAction(w6, "&Edit", "Volume &name..."), *bootFrom = menuAction(w6, "&Tools", "Copy &boot loader from...");
    CHECK(volName && volName->isEnabled() && bootFrom && !bootFrom->isEnabled(), "Volume name there (the SIR's names); the boot loader greyed");
    if (ureadme) {
        up->clearSelection();
        ureadme->setSelected(true);
        up->setCurrentItem(ureadme);
        w6->updateUi();
        CHECK(w6->action(Act::View)->isEnabled() && w6->action(Act::Extract)->isEnabled() && w6->action(Act::Properties)->isEnabled() &&
              w6->action(Act::Rename)->isEnabled() && w6->action(Act::Delete)->isEnabled(), "a file selected: View, Extract, Properties, Rename, Delete");
        w6->viewSelected();
        ViewerDialog *uv = w6->findChild<ViewerDialog *>();
        CHECK(uv && uv->viewId() == "uniflextext" && uv->shownText() == "UniFLEX test disc\n\tindented with a TAB\nlast line\n",
              "UniFLEX text: CR lines, the TAB kept: " + (uv ? uv->viewId() + " " + uv->shownText().left(40) : QString("no dialog")));
        CHECK(viewMenu(uv).count("Text") == 1, "one Text in the view menu: " + viewMenu(uv).join(", "));
        if (uv) uv->close();
    }
    QTemporaryDir uout;
    int ufiles = 0;
    QStringList uerrors;
    QJsonArray ujson;
    CHECK(w6->extractTo({dw::ui::DiscWindow::Item(0, dw::fs::Path({"src"})), dw::ui::DiscWindow::Item(0, dw::fs::Path({"Prog"}))},
                        uout.path(), &ufiles, &uerrors, &ujson, &err) && uerrors.isEmpty(), "extract src/ and Prog: " + err + uerrors.join("; "));
    CHECK(QFileInfo(uout.path() + "/Prog").size() == 768 && QFileInfo(uout.path() + "/src/sub/deep.c").size() == 30 &&
          QFileInfo(uout.path() + "/src/big.dat").size() == 160 * 512 + 33, "the tree comes out, names as they are: Prog, src/sub/deep.c, src/big.dat");
    QFile ur(uout.path() + "/src/holey");
    CHECK(ur.open(QIODevice::ReadOnly) && ur.readAll() == QByteArray(512, 'A') + QByteArray(1024, '\0') + QByteArray(100, 'B'), "holey, its unwritten blocks as zeros");
    CHECK(QFileInfo(uout.path() + "/Prog").lastModified().toUTC() == QDateTime(QDate(1985, 4, 25), QTime(19, 24, 40), QTimeZone::utc()),
          "dated from its fdn: " + QFileInfo(uout.path() + "/Prog").lastModified().toUTC().toString(Qt::ISODate));
    QMetaObject::invokeMethod(w6, "discInfo");
    CHECK(w6->statusBar()->currentMessage() == "info shown", "Disc information");
    // changes: a directory made, files copied into it, a rename in place; then saved and read again
    CHECK(w6->makeDirectory(dw::fs::Path({"src"}), "made", &err) && w6->document()->uniflex()->find({"src", "made"}) >= 0,
          "New directory: src/made: " + err);
    QDir().mkpath(work.path() + "/uhost");
    QFile uh(work.path() + "/uhost/Notes.Txt");
    if (uh.open(QIODevice::WriteOnly)) { uh.write("notes\r"); uh.close(); }
    w6->importPaths({uh.fileName()}, dw::fs::Path({"src", "made"}));
    const int un = w6->document()->uniflex()->find({"src", "made", "Notes.Txt"});
    CHECK(un >= 0, "Add files into src/made: Notes.Txt, its case kept");
    CHECK(w6->renameFile(dw::ui::DiscWindow::Item(0, dw::fs::Path({"src", "made", "Notes.Txt"})), "notes", &err) &&
          w6->document()->uniflex()->find({"src", "made", "notes"}) >= 0, "renamed in its own directory: " + err);
    CHECK(w6->windowTitle().contains("*"), "the title marks the change: " + w6->windowTitle());
    CHECK(w6->save() && !w6->windowTitle().contains("*"), "saved");
    err.clear();
    auto *fresh = new MainWindow;
    CHECK(fresh->newImage(work.path() + "/uf-new.dsk", "uniflex-ds-dd", "fresh pack", 3, QString(), &err) && fresh->hasUniflex() &&
          fresh->document()->uniflex()->sir().packNameText() == "fresh pack" && fresh->document()->uniflex()->sir().number == 3 &&
          fresh->document()->uniflex()->check().ok() && fresh->writable(0) && QFileInfo(work.path() + "/uf-new.dsk").size() == 2464 * 512,
          "New disc image, UniFLEX double-sided double density: 2464 blocks, the pack name and number, clean, writable: " + err);
    delete fresh;
    auto *again = new MainWindow;
    CHECK(again->openFile(work.path() + "/uf-basic.dsk") && again->document()->uniflex()->find({"src", "made", "notes"}) >= 0 &&
          again->document()->uniflex()->check().ok(), "read again: the change is there, the disc checks clean");
    delete again;
    w6->checkDisc();
    CHECK(w6->document()->disc().validateAll().ok(), "Check disc: clean");
    w6->editProperties();
    CHECK(w6->statusBar()->currentMessage() == "properties shown", "Properties");
    delete w6;

    auto *w7 = new MainWindow;
    CHECK(w7->openFile(dir + "/uf-bad-map.dsk") && w7->hasUniflex() && w7->statusBar()->currentMessage().contains("the disc has errors"),
          "a damaged UniFLEX disc opens, its errors in the status line: " + w7->statusBar()->currentMessage());
    QTreeWidgetItem *bigRow = childNamed(childNamed(w7->pane("uniflex")->topLevelItem(0), "src"), "big.dat");
    CHECK(bigRow && bigRow->toolTip(0).contains("outside the volume"), "the broken file says why: " + (bigRow ? bigRow->toolTip(0) : QString()));
    delete w7;

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
