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
#include "mainwindow.h"

#include "hostpolicy.h"
#include "viewer.h"

#include <diskwrangler/app/appsettings.h>
#include <diskwrangler/ui/buttonbar.h>
#include <diskwrangler/ui/dragoutmime.h>
#include <diskwrangler/ui/licences.h>
#include <diskwrangler/ui/treecolumns.h>
#include <diskwrangler/ui/uihelpers.h>
#include <diskwrangler/xfer/mime.h>
#include <diskwrangler/xfer/retrofiles.h>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QStatusBar>
#include <QTextBrowser>
#include <QVBoxLayout>

// ================================================================================================ the pane

FlexPane::FlexPane(QWidget *parent) : dw::ui::VolumePane(parent) {
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(true);
}

QMimeData *FlexPane::makeMime(const QList<QTreeWidgetItem *> &rows) const {
    MainWindow *win = MainWindow::byId(sourceWindow());
    if (!win || !win->hasDisc()) return nullptr;
    QVector<dw::ui::DiscWindow::Item> items;
    for (QTreeWidgetItem *it : rows) {
        const dw::ui::PaneRow r = rowOf(it);
        if (r.isFile()) items << dw::ui::DiscWindow::Item(r.mount, r.path);
        else if (r.root)
            for (const Flex::File &f : win->document()->disk()->files()) items << dw::ui::DiscWindow::Item(r.mount, dw::fs::Path({f.name}));
    }
    if (items.isEmpty()) return nullptr;
    QPointer<MainWindow> pw = win;
    dw::DragOutMime::Exporter exporter = [pw, items](QVector<dw::TransferItem> *out, QString *err) {
        if (!pw) { *err = "the disc's window was closed during the drag"; return false; }
        return pw->exportItems(items, out, err);
    };
    auto *m = new dw::DragOutMime(exporter, win->dragSpec());
    m->setData(DW_MIME_SOURCE, sourceTag());
    return m;
}

void FlexPane::startDrag(Qt::DropActions) {
    QMimeData *data = makeMime(selectedItems());
    if (!data) return;
    auto *drag = new QDrag(this);
    drag->setMimeData(data);
    if (QTreeWidgetItem *first = currentItem()) {
        QRect r = visualItemRect(first).intersected(viewport()->rect());
        if (r.isValid()) {
            r.setWidth(qMin(r.width(), columnWidth(0)));
            drag->setPixmap(viewport()->grab(r));
            drag->setHotSpot(viewport()->mapFromGlobal(QCursor::pos()) - r.topLeft());
        }
    }
    execDragOut(drag);       // copy only; never deleted here: on X11 the target fetches the files after exec() returns
}

// A window with no FLEX disc opens dropped images; one with a disc takes nothing (series flex-1 only reads).
dw::ui::VolumePane::DropRules FlexPane::dropRules() const {
    DropRules r;
    MainWindow *win = MainWindow::byId(sourceWindow());
    r.filesOnly = true;
    r.take = win && !win->hasDisc();
    return r;
}

// ================================================================================================ the window

MainWindow *MainWindow::byId(quint64 id) { return qobject_cast<MainWindow *>(DiscWindow::byId(id)); }

dw::app::AppInfo MainWindow::appInfo() const { return Flex::appInfo(QApplication::applicationVersion()); }

dw::ui::DiscWindow::ActionSpec MainWindow::actionSpec(Act a) const {
    auto key = [](QKeySequence::StandardKey k) { return QList<QKeySequence>{QKeySequence(k)}; };
    switch (a) {
    case Act::NewWindow: return {"New &window", {QKeySequence("Ctrl+Shift+N")}, QString(), QString()};
    case Act::NewImage: return {"&New disc image...", key(QKeySequence::New), QString(), QString()};
    case Act::Open: return {"&Open...", key(QKeySequence::Open), QString(), QString()};
    case Act::Save: return {"&Save", key(QKeySequence::Save), QString(), QString()};
    case Act::SaveAs: return {"Save &as...", {QKeySequence("Ctrl+Shift+S")}, QString(), QString()};
    case Act::Revert: return {"&Revert", {}, QString(), QString()};
    case Act::Close: return {"&Close", key(QKeySequence::Close), QString(), QString()};
    case Act::Quit: return {"&Quit", key(QKeySequence::Quit), QString(), QString()};
    case Act::View: return {"&View (text / hex)", {QKeySequence("F3")}, "View", QString()};
    case Act::Extract: return {"E&xtract to folder...", {QKeySequence("Ctrl+E")}, "Extract", QString()};
    case Act::AddFiles: return {"&Add files...", {QKeySequence("Ctrl+Shift+F")}, "Add files", QString()};
    case Act::Copy: return {"&Copy", QKeySequence::keyBindings(QKeySequence::Copy), QString(), QString()};
    case Act::Paste: return {"&Paste", QKeySequence::keyBindings(QKeySequence::Paste), QString(), QString()};
    case Act::Rename: return {"Re&name...", {QKeySequence("F2")}, "Rename", QString()};
    case Act::Properties: return {"P&roperties...", {QKeySequence("Alt+Return")}, "Properties", QString()};
    case Act::Delete: return {"&Delete", dw::deleteKeys(), "Delete", QString()};
    default: return {};                // no Add folder or New directory: FLEX has no folders
    }
}

dw::ui::PaneLayout MainWindow::paneLayout(const QString &) const {
    dw::ui::PaneLayout l;
    l.columns = {{"Name", "", false}, {"Attr", "WDRC", false}, {"Size", "1048576", true},
                 {"Sectors", "00000", true}, {"Type", "Random", false}, {"Date", "1984-12-31", false}};
    l.expand = dw::ui::PaneLayout::ExpandAll;
    l.reselectFolders = false;
    return l;
}

dw::ui::VolumePane *MainWindow::createPane(const QString &) {
    auto *t = new FlexPane;
    t->setDragSource(id(), 0);
    return t;
}

QList<QAction *> MainWindow::contextActions() const {     // the Edit menu's order
    return {action(Act::View), action(Act::Extract), nullptr, action(Act::AddFiles), nullptr, action(Act::Copy), action(Act::Paste),
            nullptr, action(Act::Rename), action(Act::Properties), action(Act::Delete)};
}

MainWindow::MainWindow(QWidget *parent) : DiscWindow(parent) {
    setupKit();
    resize(820, 560);

    // The menus, the context menu and the button bar keep the family's order (CP/M Disk Wrangler's, which this follows).
    QMenu *file = menuBar()->addMenu("&File");
    file->addActions({action(Act::NewWindow), action(Act::NewImage), action(Act::Open)});
    file->addSeparator();
    file->addActions({action(Act::Save), action(Act::SaveAs), action(Act::Revert)});
    file->addSeparator();
    AppSettings::addMenuItem(file, this);
    if (AppSettings::offered()) file->addSeparator();
    file->addActions({action(Act::Close), action(Act::Quit)});

    QMenu *edit = menuBar()->addMenu("&Edit");
    edit->addActions({action(Act::View), action(Act::Extract)});
    edit->addSeparator();
    edit->addAction(action(Act::AddFiles));
    edit->addSeparator();
    edit->addActions({action(Act::Copy), action(Act::Paste)});
    edit->addSeparator();
    edit->addActions({action(Act::Rename), action(Act::Properties), action(Act::Delete)});
    edit->addSeparator();
    m_actLabel = edit->addAction("Volume &name...", this, [this] { editVolume(0, "label"); });

    QMenu *tools = menuBar()->addMenu("&Tools");
    m_actCheck = tools->addAction("&Check disc", this, &MainWindow::checkDisc);
    m_actCheck->setShortcut(QKeySequence("Ctrl+K"));
    m_actInfo = tools->addAction("Disc &information", this, &MainWindow::discInfo);
    m_actInfo->setShortcut(QKeySequence("Ctrl+I"));
    tools->addSeparator();
    m_optBackup = tools->addAction("Keep a .bak copy when saving");
    m_optBackup->setCheckable(true);
    m_optBackup->setChecked(AppSettings().value("backup", true).toBool());
    connect(m_optBackup, &QAction::toggled, [](bool on) { AppSettings().setValue("backup", on); });

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("&About FLEX Disk Wrangler...", this, &MainWindow::about);
    help->addAction("About &Qt...", qApp, &QApplication::aboutQt);
    help->addAction("&Licences...", this, [this] { showLicences(this); });

    ButtonBar::makeButtonBar(addToolBar("Main"), {action(Act::View), action(Act::Extract), action(Act::AddFiles), action(Act::Rename),
                                                  action(Act::Properties), action(Act::Delete)});
    connect(flexPane(), &FlexPane::dragOutFinished, this, [this](int files, int, const QStringList &errors) {
        if (!errors.isEmpty()) message("Drag", errors.join("\n"));
        else showMessage(QString("Copied %1 file%2 out of the disc.").arg(files).arg(files == 1 ? "" : "s"));
    });
    connect(flexPane(), &FlexPane::dragOutNothing, this, [this] {
        showMessage("No files were copied out of the disc: the drop wasn't taken, or no file was asked for.");
    });
    refresh();
}

MainWindow::~MainWindow() = default;

dw::DragSpec MainWindow::dragSpec() const {
    dw::DragSpec s;
    s.policy = std::make_shared<Flex::HostPolicy>();
    s.filesystem = "flex";
    s.generator = "FLEX Disk Wrangler";
    s.imageName = QFileInfo(m_doc.path()).fileName();
    s.tempPrefix = dw::xfer::dragDirPrefix("flexdiskwrangler");
    return s;
}

// ------------------------------------------------------------------------------------------- opening

void MainWindow::openDialog() {
    const QString p = QFileDialog::getOpenFileName(this, "Open a FLEX disc image", AppSettings().value("lastDir").toString(),
                                                   "FLEX disc images (" + dw::anyCase("*.dsk *.flx *.img") + ");;All files (*)");
    if (p.isEmpty()) return;
    AppSettings().setValue("lastDir", QFileInfo(p).absolutePath());
    openInWindow(p);
}

bool MainWindow::detectImage(const QString &path, int, dw::fs::Verdict *verdict) {
    QString err;
    if (!m_doc.load(path, &err)) { message("Open", QString("%1:\n%2").arg(path, err)); return false; }
    m_notFlex.clear();
    *verdict = Flex::verdict(m_doc.container(), path);
    return true;
}

bool MainWindow::openDocument(const QString &path, const dw::fs::Verdict &, const dw::fs::Candidate *pick, int) {
    QString why;
    if (!pick) why = "it isn't a FLEX disc: no FLEX System Information Record that fits the file";
    else if (!pick->supported) why = "it is " + pick->description + ", which FLEX Disk Wrangler can't open";
    else {
        QString err;
        if (m_doc.attach(std::any_cast<Flex::ProbeResult>(pick->data), &err)) return true;
        why = err;
    }
    m_notFlex = why;
    openStopped();
    NotOpened m;
    m.title = "Open";
    m.text = QString("%1\n\nNot read as FLEX: %2.").arg(QFileInfo(path).fileName(), why);
    m.warning = true;
    showNotOpened(path, m);
    return false;
}

void MainWindow::openStopped() {
    m_doc.detach();
    refresh();
}

void MainWindow::afterOpen(const QString &) { refresh(); }

// ------------------------------------------------------------------------------------------- saving

bool MainWindow::keepBackup() const { return m_optBackup->isChecked(); }

bool MainWindow::askSaveAs(QString *path, dw::device::Kind *kind) {
    QString filter = "Raw FLEX image (" + dw::anyCase("*.dsk *.flx *.img") + ")";
    const QString p = QFileDialog::getSaveFileName(this, "Save as", QFileInfo(m_doc.path()).absolutePath() + "/" +
                                                   QFileInfo(m_doc.path()).completeBaseName() + "-copy." + QFileInfo(m_doc.path()).suffix(),
                                                   filter, &filter);
    if (p.isEmpty()) return false;
    *path = p;
    *kind = dw::device::Kind::Raw;
    return true;
}

bool MainWindow::afterSaveAs(const QString &, QString *err) { return !m_doc.disk() || m_doc.disk()->reload(err); }

// ------------------------------------------------------------------------------------------- the list

QString MainWindow::rootLabel(int) const {
    const QString title = m_doc.disk() ? m_doc.disk()->sir().labelText() : QString();
    return dw::rootLabel(QFileInfo(m_doc.path()).fileName(), title);
}

void MainWindow::decorateRow(QTreeWidgetItem *it, int, const dw::fs::Path &, const dw::fs::Entry *e) const {
    if (!e) return;
    it->setText(0, e->name);
    QString attr;
    if (e->attrs.value("write").toBool()) attr += 'W';
    if (e->attrs.value("delete").toBool()) attr += 'D';
    if (e->attrs.value("read").toBool()) attr += 'R';
    if (e->attrs.value("catalog").toBool()) attr += 'C';
    it->setText(1, attr);
    dw::setNumCell(it, 2, QString::number(e->size));
    dw::setNumCell(it, 3, QString::number(e->attrs.value("sectors").toInt()));
    it->setText(4, e->attrs.value("random").toBool() ? "Random" : QString());
    if (e->modified.isValid()) it->setText(5, e->modified.date().toString("yyyy-MM-dd"));
    if (!e->problem.isEmpty()) {
        it->setToolTip(0, e->problem);
        it->setForeground(0, QColor("#b00000"));
    }
}

void MainWindow::updateActions() {
    DiscWindow::updateActions();
    if (!m_actCheck) return;                            // the constructor hasn't made them all yet
    action(Act::NewImage)->setEnabled(false);           // new discs: series flex-2
    action(Act::Paste)->setEnabled(false);
    m_actCheck->setEnabled(hasDisc());
    m_actInfo->setEnabled(hasDisc());
    m_actLabel->setEnabled(hasDisc() && writable(0));
}

QStringList MainWindow::statusVariants() const {
    if (!hasDisc()) {
        if (m_doc.isOpen() && !m_notFlex.isEmpty())
            return {"Not read as FLEX: " + m_notFlex + ". Drop another image here to open it.", "Not read as FLEX"};
        return {"Open a FLEX disc image (File > Open), or drop one here.", "Open or drop a disc image."};
    }
    const Flex::Disk &d = *m_doc.disk();
    const int n = d.files().size();
    const QString files = QString("%1 file%2").arg(n).arg(n == 1 ? "" : "s");
    const QString fr = dw::fmtBytes(qint64(d.sir().freeCount) * Flex::DataBytes, dw::SmallSizes::AsBytes);
    const QString shape = QString("FLEX, %1 tracks of %2 sectors").arg(d.tracks()).arg(d.sectorsPerTrack());
    QString longTail, shortTail;
    if (!writable(0)) { longTail = "  -  read-only: " + readOnlyReason(0); shortTail = "  -  read-only"; }
    return {QString("%1 - %2, %3 free (%4 sectors)").arg(shape, files, fr).arg(d.sir().freeCount) + longTail,
            QString("%1 - %2, %3 free").arg(shape, files, fr) + shortTail,
            QString("%1, %2 free").arg(files, fr) + shortTail};
}

// ------------------------------------------------------------------------------------------- copying out

std::shared_ptr<dw::xfer::HostPolicy> MainWindow::hostPolicy(const QVector<dw::TransferItem> &) const {
    return std::make_shared<Flex::HostPolicy>();
}

void MainWindow::viewSelected() {
    const QVector<Item> items = selectedFiles();
    if (items.size() != 1) return;
    QVector<dw::TransferItem> files;
    QString err;
    if (!exportItems(items, &files, &err)) { message("View", err); return; }
    auto *v = new ViewerDialog(items[0].path.name(), files[0].data, this);
    v->setAttribute(Qt::WA_DeleteOnClose);
    v->show();
}

void MainWindow::copySelected() {
    const QVector<Item> chosen = selectedFiles();
    if (chosen.isEmpty()) { showMessage("Select the files to copy."); return; }
    QVector<dw::TransferItem> items;
    QString err;
    if (!exportItems(chosen, &items, &err)) { message("Copy", err); return; }
    const int n = publishCopy(items, dragSpec());
    showMessage(QString("Copied %1 file%2 - paste %3 into a folder on the desktop or another program.")
                    .arg(n).arg(n == 1 ? "" : "s").arg(n == 1 ? "it" : "them"));
}

// Properties: what the directory says about each file. Nothing can be changed yet (series flex-1).
bool MainWindow::propertiesEditor(const QVector<Item> &items, PropertiesEdit *) {
    if (!hasDisc() || items.isEmpty()) return false;
    QStringList l;
    for (const Item &i : items.mid(0, 20)) {
        const int k = m_doc.disk()->indexOf(i.path.name());
        if (k < 0) continue;
        const Flex::File &f = m_doc.disk()->files()[k];
        QStringList prot;
        if (f.attr & Flex::WriteProtect) prot << "write";
        if (f.attr & Flex::DeleteProtect) prot << "delete";
        if (f.attr & Flex::ReadProtect) prot << "read";
        if (f.attr & Flex::CatalogProtect) prot << "catalogue";
        const QDate d = f.date();
        l << QString("<b>%1</b><br>%2 sectors (%3 bytes of data)%4; from %5 to %6<br>Date: %7<br>Protected against: %8%9")
                 .arg(f.name.toHtmlEscaped()).arg(f.sectors).arg(f.size())
                 .arg(f.isRandom() ? ", random access (2 map sectors)" : QString())
                 .arg(f.start.toString(), f.end.toString())
                 .arg(d.isValid() ? d.toString("d MMMM yyyy") : QString("none"))
                 .arg(prot.isEmpty() ? QString("nothing") : prot.join(", "))
                 .arg(f.problem.isEmpty() ? QString() : "<br><font color=#b00000>" + f.problem.toHtmlEscaped() + "</font>");
    }
    if (items.size() > 20) l << QString("... and %1 more").arg(items.size() - 20);
    if (autoAnswer) { showMessage("properties shown"); return false; }
    QMessageBox box(QMessageBox::Information, "Properties", l.join("<br><br>"), QMessageBox::Close, this);
    box.setTextFormat(Qt::RichText);
    box.setInformativeText(QString("Read-only: ") + Flex::FlexVolume::ReadOnlyText + ".");
    box.exec();
    return false;
}

void MainWindow::discInfo() {
    if (!hasDisc()) return;
    const Flex::Disk &d = *m_doc.disk();
    const Flex::Sir &s = d.sir();
    QStringList l;
    l << "<h3>" + QFileInfo(m_doc.path()).fileName().toHtmlEscaped() + "</h3>";
    l << QString("<b>Container:</b> %1, %2 bytes").arg(dw::device::kindName(m_doc.container().kind())).arg(m_doc.container().rawFileSize());
    l << QString("<b>Geometry (from the SIR):</b> %1 tracks (0-%2) of %3 sectors of 256 bytes").arg(d.tracks()).arg(s.lastTrack).arg(s.sectors);
    l << QString("<b>Volume:</b> \"%1\", number %2").arg(s.labelText().toHtmlEscaped()).arg(s.volumeNumber);
    l << QString("<b>Made:</b> %1").arg(s.date().isValid() ? s.date().toString("d MMMM yyyy") : QString("no date"));
    l << QString("<b>Free:</b> %1 sectors, the free chain from %2 to %3").arg(s.freeCount).arg(s.firstFree.toString(), s.lastFree.toString());
    l << QString("<b>Directory:</b> %1 sectors, %2 files, %3 deleted entries, %4 free slots")
             .arg(d.dirSectors().size()).arg(d.files().size()).arg(d.deletedEntries()).arg(d.freeSlots());
    if (autoAnswer) { showMessage("info shown"); return; }
    QDialog dlg(this);
    dlg.setWindowTitle("Disc information");
    dlg.resize(560, 360);
    auto *tb = new QTextBrowser;
    tb->setHtml(l.join("<br>"));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(tb);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

void MainWindow::about() {
    QMessageBox::about(this, "About FLEX Disk Wrangler",
                       "<b>FLEX Disk Wrangler</b><br>Version " + QApplication::applicationVersion().toHtmlEscaped() +
                           "<br><br>View disc images of TSC FLEX, the disc operating system of 6800 and 6809 computers: "
                           "list, view and copy out their files, and check their directories and free chains. "
                           "Exchanges files with the other Disk Wranglers and other programs by drag and drop (retro-files format).<br><br>"
                           "Copyright &copy; 2026 Daniel Jameson.<br>Licensed under the GNU General Public License v3 or later.<br><br>"
                           "Uses Qt under the LGPL v3. Help &gt; Licences shows the full licence texts and notices, including those of "
                           "the libraries distributed with it.<br><br>" +
                           AppSettings::where().toHtmlEscaped());
}
