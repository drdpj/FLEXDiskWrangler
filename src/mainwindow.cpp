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
#include <diskwrangler/app/clock.h>
#include <diskwrangler/ui/buttonbar.h>
#include <diskwrangler/ui/dragoutmime.h>
#include <diskwrangler/ui/licences.h>
#include <diskwrangler/ui/treecolumns.h>
#include <diskwrangler/ui/uihelpers.h>
#include <diskwrangler/xfer/mime.h>
#include <diskwrangler/xfer/hostread.h>
#include <diskwrangler/xfer/retrofiles.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
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

// A window with no FLEX disc opens dropped images; one with a disc that can be written imports what is dropped on it
// (files, and retro-files JSON from another window or program); a read-only disc takes nothing.
dw::ui::VolumePane::DropRules FlexPane::dropRules() const {
    DropRules r;
    MainWindow *win = MainWindow::byId(sourceWindow());
    r.filesOnly = win && !win->hasDisc();
    r.take = win && (!win->hasDisc() || win->writable(0));
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
    m_actBoot = tools->addAction("Copy &boot loader from...", this, &MainWindow::copyBootLoaderDialog);
    m_actLink = tools->addAction("&Link boot loader to file", this, &MainWindow::linkBootLoaderSelected);
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
    connect(flexPane(), &FlexPane::retroFilesDropped, this, [this](const QByteArray &json, QTreeWidgetItem *) {
        QTimer::singleShot(0, this, [this, json] { importJson(json); });     // no dialogs inside the drop handler
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
    m_actCheck->setEnabled(hasDisc());
    m_actInfo->setEnabled(hasDisc());
    m_actLabel->setEnabled(hasDisc() && writable(0));
    m_actBoot->setEnabled(hasDisc() && writable(0));
    const QVector<dw::ui::PaneRow> rows = selectedRows();
    m_actLink->setEnabled(hasDisc() && writable(0) && rows.size() == 1 && rows.first().isFile());
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

// Properties: what the directory says about each file, its name (one file) and its protection; the kit applies what
// changes in one transaction. The random-access flag and the sectors are shown, not changed.
bool MainWindow::propertiesEditor(const QVector<Item> &items, PropertiesEdit *edit) {
    if (!hasDisc() || items.isEmpty()) return false;
    if (autoAnswer) { showMessage("properties shown"); return false; }
    QVector<Flex::File> files;
    for (const Item &i : items) {
        const int k = m_doc.disk()->indexOf(i.path.name());
        if (k >= 0) files << m_doc.disk()->files()[k];
    }
    if (files.isEmpty()) return false;
    const bool w = writable(0);
    QDialog d(this);
    d.setWindowTitle(files.size() == 1 ? "Properties - " + files[0].name : QString("Properties - %1 files").arg(files.size()));
    auto *fl = new QFormLayout;
    QLineEdit *name = nullptr;
    if (files.size() == 1) {
        const Flex::File &f = files[0];
        name = new QLineEdit(f.name);
        name->setReadOnly(!w);
        fl->addRow("Name", name);
        fl->addRow("Size", new QLabel(QString("%1 sectors, %2 bytes of data%3").arg(f.sectors).arg(f.size())
                                          .arg(f.isRandom() ? "; random access (2 map sectors)" : QString())));
        fl->addRow("Sectors", new QLabel(QString("from %1 to %2").arg(f.start.toString(), f.end.toString())));
        const QDate dt = f.date();
        fl->addRow("Date", new QLabel(dt.isValid() ? dt.toString("d MMMM yyyy") : QString("none")));
        if (!f.problem.isEmpty()) {
            auto *pl = new QLabel(f.problem);
            pl->setStyleSheet("color: #b00000");
            fl->addRow("Problem", pl);
        }
    }
    struct Box { quint8 bit; const char *key, *label; QCheckBox *cb; };
    QVector<Box> boxes = {{Flex::WriteProtect, "write", "Write-protected (W)", nullptr},
                          {Flex::DeleteProtect, "delete", "Delete-protected (D)", nullptr},
                          {Flex::ReadProtect, "read", "Read-protected (R)", nullptr},
                          {Flex::CatalogProtect, "catalog", "Catalogue-protected: left out of CAT (C)", nullptr}};
    for (Box &b : boxes) {
        b.cb = new QCheckBox(b.label);
        int on = 0;
        for (const Flex::File &f : files) if (f.attr & b.bit) ++on;
        if (on == 0) b.cb->setCheckState(Qt::Unchecked);
        else if (on == files.size()) b.cb->setCheckState(Qt::Checked);
        else { b.cb->setTristate(true); b.cb->setCheckState(Qt::PartiallyChecked); }
        b.cb->setEnabled(w);
        fl->addRow(b.cb);
    }
    auto *bb = new QDialogButtonBox(w ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel : QDialogButtonBox::Close);
    auto *lay = new QVBoxLayout(&d);
    lay->addLayout(fl);
    if (!w) lay->addWidget(new QLabel("Read-only: " + readOnlyReason(0)));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted || !w) return false;
    for (const Flex::File &f : files) {
        PropertyChange c;
        c.item = Item(0, dw::fs::Path({f.name}));
        for (const Box &b : boxes) {
            if (b.cb->checkState() == Qt::PartiallyChecked) continue;
            const bool want = b.cb->checkState() == Qt::Checked;
            if (want != bool(f.attr & b.bit)) c.attrs[b.key] = want;
        }
        const QString newName = name ? name->text().trimmed().toUpper() : f.name;
        if (!newName.isEmpty() && newName != f.name) c.to = dw::fs::Path({newName});
        edit->changes << c;
    }
    return true;
}

// Edit > Volume name: the name (up to 8 characters; the 3 bytes after it, a formatter's tag, are kept) and the number.
bool MainWindow::volumeEditor(int, const QString &, VolumeEdit *edit) {
    if (!hasDisc() || autoAnswer) return false;
    const Flex::Sir &s = m_doc.disk()->sir();
    QDialog d(this);
    d.setWindowTitle("Volume name");
    auto *fl = new QFormLayout;
    auto *name = new QLineEdit(s.labelText());
    name->setMaxLength(Flex::VolumeNameLength);
    auto *number = new QSpinBox;
    number->setRange(0, 65535);
    number->setValue(s.volumeNumber);
    fl->addRow("Volume name", name);
    fl->addRow("Volume number", number);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *lay = new QVBoxLayout(&d);
    lay->addLayout(fl);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return false;
    edit->values = {{"label", name->text()}, {"number", number->value()}};
    edit->title = "Volume name";
    return true;
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
    QString boot = "none (the disc doesn't start FLEX)";
    if (d.hasLoader()) {
        const Flex::TS link = d.bootLink();
        QString to;
        for (const Flex::File &f : d.files()) if (f.start == link) to = f.name;
        boot = link.isNull() ? QString("a loader, not linked")
             : to.isEmpty() ? QString("a loader, linked to %1, where no file starts").arg(link.toString())
                            : QString("a loader, linked to %1 (%2)").arg(to.toHtmlEscaped(), link.toString());
    }
    l << "<b>Boot loader:</b> " + boot;
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
                           "<br><br>Work with disc images of TSC FLEX, the disc operating system of 6800 and 6809 computers: "
                           "list, view, copy out, add, rename, protect and delete their files, make new discs, and check "
                           "their directories and free chains, changing them as FLEX itself does. "
                           "Exchanges files with the other Disk Wranglers and other programs by drag and drop (retro-files format).<br><br>"
                           "Copyright &copy; 2026 Daniel Jameson.<br>Licensed under the GNU General Public License v3 or later.<br><br>"
                           "Uses Qt under the LGPL v3. Help &gt; Licences shows the full licence texts and notices, including those of "
                           "the libraries distributed with it.<br><br>" +
                           AppSettings::where().toHtmlEscaped());
}

// ------------------------------------------------------------------------------------------- new discs

// File > New disc image: a shape of the sample discs, the volume name and number, and a boot loader copied from a disc
// the user has (none: the disc reads and writes under FLEX but doesn't start it). FLEX's own loaders are TSC's code,
// so the program carries none.
void MainWindow::newImageDialog() {
    AppSettings st;
    QDialog d(this);
    d.setWindowTitle("New disc image");
    auto *fl = new QFormLayout;
    auto *shape = new QComboBox;
    const QVector<dw::fs::Preset> ps = Flex::presets();
    for (const dw::fs::Preset &p : ps) shape->addItem(p.name, p.id);
    const int was = shape->findData(st.value("newPreset", "flex-40x10").toString());
    if (was >= 0) shape->setCurrentIndex(was);
    auto *name = new QLineEdit;
    name->setMaxLength(Flex::VolumeNameLength);
    auto *number = new QSpinBox;
    number->setRange(0, 65535);
    auto *boot = new QLineEdit(st.value("bootSource").toString());
    boot->setPlaceholderText("none");
    auto *browse = new QPushButton("Browse...");
    auto *bootRow = new QHBoxLayout;
    bootRow->addWidget(boot);
    bootRow->addWidget(browse);
    connect(browse, &QPushButton::clicked, &d, [&] {
        const QString p = QFileDialog::getOpenFileName(&d, "Copy the boot loader from a FLEX disc image", QFileInfo(boot->text()).absolutePath(),
                                                       "FLEX disc images (" + dw::anyCase("*.dsk *.flx *.img") + ");;All files (*)");
        if (!p.isEmpty()) boot->setText(p);
    });
    fl->addRow("Shape", shape);
    fl->addRow("Volume name", name);
    fl->addRow("Volume number", number);
    fl->addRow("Boot loader from", bootRow);
    auto *note = new QLabel("Laid out as FLEX's NEWDISK lays a disc out. FLEX's boot loader is TSC's code, so it is copied "
                            "from a FLEX disc you have (track 0, sectors 1 and 2); without one the disc works under FLEX but "
                            "doesn't start it. Link the loader once FLEX.SYS is on the disc (Tools > Link boot loader to file).");
    note->setWordWrap(true);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *lay = new QVBoxLayout(&d);
    lay->addLayout(fl);
    lay->addWidget(note);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    const QString id = shape->currentData().toString();
    st.setValue("newPreset", id);
    st.setValue("bootSource", boot->text().trimmed());
    const QString base = name->text().trimmed().isEmpty() ? QString("flex") : name->text().trimmed().toLower();
    const QString p = QFileDialog::getSaveFileName(this, "New disc image", st.value("lastDir").toString() + "/" + base + ".dsk",
                                                   "Raw FLEX image (" + dw::anyCase("*.dsk *.flx *.img") + ")");
    if (p.isEmpty()) return;
    st.setValue("lastDir", QFileInfo(p).absolutePath());
    QString err;
    if (!newImage(p, id, name->text(), number->value(), boot->text().trimmed(), &err)) message("New disc image", err);
}

bool MainWindow::newImage(const QString &path, const QString &presetId, const QString &name, int number, const QString &bootSource, QString *err) {
    const dw::fs::Preset *p = Flex::findPreset(presetId);
    if (!p) { *err = "no such shape: " + presetId; return false; }
    QByteArray boot;
    if (!bootSource.isEmpty() && !Flex::readBootLoader(bootSource, &boot, err)) return false;
    if (!Flex::createBlank(path, *p, name, number, dw::now().date(), boot, err)) return false;
    MainWindow *w = m_doc.isOpen() ? static_cast<MainWindow *>(createWindow()) : this;
    if (!w->openFile(path)) { *err = path + " was made but couldn't be opened"; return false; }
    w->showMessage(QString("Made %1: %2%3.").arg(QFileInfo(path).fileName(), p->name, boot.isEmpty() ? QString(", no boot loader") : ", with the boot loader of " + QFileInfo(bootSource).fileName()));
    return true;
}

// ------------------------------------------------------------------------------------------- copying in

// Host files (Add files, a drop, Paste of a file manager's copy), read by the core's one reader with this program's
// size limit; a folder's files are added flat (FLEX has no folders).
void MainWindow::importPaths(const QStringList &paths) {
    if (!hasDisc()) return;
    QVector<dw::xfer::HostReadError> errors;
    const QVector<dw::TransferItem> items = dw::xfer::readHost(paths, Flex::hostRead(), &errors);
    QVector<dw::xfer::ImportError> readErrors;
    for (const dw::xfer::HostReadError &e : errors) {
        dw::xfer::ImportError ie;
        ie.kind = dw::xfer::ImportError::Failed;
        ie.why = e.kind == dw::xfer::HostReadError::TooLarge ? "too large for a FLEX disc"
               : e.kind == dw::xfer::HostReadError::NotFound ? "no such file or folder" : "cannot read (" + e.reason + ")";
        ie.message = QFileInfo(e.path).fileName() + ": " + ie.why;
        ie.from = e.path;
        readErrors << ie;
    }
    if (items.isEmpty() && readErrors.isEmpty()) return;
    importItems(0, dw::fs::Path(), items, {}, readErrors);
}

// Retro-files JSON: dropped from another window or program, or pasted.
void MainWindow::importJson(const QByteArray &json) {
    if (!hasDisc()) return;
    QVector<dw::TransferItem> items;
    QString err;
    if (!dw::xfer::readJson(json, &items, &err)) { message("Add files", err); return; }
    importItems(0, dw::fs::Path(), items);
}

void MainWindow::addFilesDialog() {
    if (!hasDisc()) return;
    const QStringList p = QFileDialog::getOpenFileNames(this, "Add files", AppSettings().value("addDir").toString());
    if (p.isEmpty()) return;
    AppSettings().setValue("addDir", QFileInfo(p.first()).absolutePath());
    importPaths(p);
}

void MainWindow::droppedOnDisc(dw::ui::VolumePane *, QTreeWidgetItem *, const QStringList &paths) {
    const QStringList p = paths;
    QTimer::singleShot(0, this, [this, p] { importPaths(p); });           // no dialogs inside the drop handler
}

// Edit > Paste: this program's own copy, retro-files JSON, or a file manager's files.
void MainWindow::paste() {
    if (!hasDisc()) return;
    if (!writable(0)) { message("Paste", "The disc is read-only: " + readOnlyReason(0)); return; }
    PasteTo to;
    to.copied = [this](const QVector<dw::TransferItem> &items) { importItems(0, dw::fs::Path(), items); };
    to.retroFiles = [this](const QByteArray &json) { importJson(json); };
    to.paths = [this](const QStringList &paths) { importPaths(paths); };
    if (!pasteClipboard(to)) showMessage("Nothing to paste: copy some files first (here, in another window, or in a file manager).");
}

// ------------------------------------------------------------------------------------------- changing files

void MainWindow::renameSelected() {
    const QVector<Item> items = selectedFiles();
    if (items.size() != 1) return;
    bool ok = false;
    const QString n = QInputDialog::getText(this, "Rename", QString("New name for %1 (up to 8 characters, a dot, up to 3):").arg(items[0].path.name()),
                                            QLineEdit::Normal, items[0].path.name(), &ok);
    if (!ok || n.trimmed().isEmpty()) return;
    QString err;
    if (!renameFile(items[0], n.trimmed(), &err)) message("Rename", err);
}

bool MainWindow::renameFile(const Item &item, const QString &newName, QString *err) {
    if (!writable(item.mount)) { *err = "read-only: " + readOnlyReason(item.mount); return false; }
    const bool done = m_doc.disc().transact([&](QString *e) {
        dw::fs::Volume *v = m_doc.disc().volume(item.mount);
        return v && v->rename(item.path, dw::fs::Path({newName}), e);
    }, err);
    refresh();
    return done;
}

// ------------------------------------------------------------------------------------------- the boot loader

void MainWindow::copyBootLoaderDialog() {
    AppSettings st;
    const QString p = QFileDialog::getOpenFileName(this, "Copy the boot loader from a FLEX disc image", st.value("bootSource").toString(),
                                                   "FLEX disc images (" + dw::anyCase("*.dsk *.flx *.img") + ");;All files (*)");
    if (p.isEmpty()) return;
    st.setValue("bootSource", p);
    QString done, err;
    if (!copyBootLoader(p, &done, &err)) message("Copy boot loader", err);
    else message("Copy boot loader", done, false);
}

bool MainWindow::copyBootLoader(const QString &image, QString *done, QString *err) {
    if (!hasDisc() || !writable(0)) { *err = "The disc is read-only: " + readOnlyReason(0); return false; }
    QByteArray boot;
    if (!Flex::readBootLoader(image, &boot, err)) return false;
    const int k = m_doc.disk()->indexOf("FLEX.SYS");
    const Flex::TS link = k >= 0 ? m_doc.disk()->files()[k].start : Flex::TS();
    const bool ok = m_doc.disc().transact([&](QString *e) { return m_doc.disk()->setBootSectors(boot, link, e); }, err);
    refresh();
    if (!ok) return false;
    *done = QString("The boot loader of %1 is copied in, ").arg(QFileInfo(image).fileName()) +
            (k >= 0 ? QString("linked to FLEX.SYS (%1).").arg(link.toString())
                    : QString("not linked: this disc has no FLEX.SYS. Add one, select it and use Tools > Link boot loader to file."));
    return true;
}

void MainWindow::linkBootLoaderSelected() {
    const QVector<Item> items = selectedFiles();
    if (items.size() != 1) return;
    QString err;
    if (!linkBootLoader(items[0], &err)) message("Link boot loader", err);
    else showMessage(QString("The boot loader starts %1 now.").arg(items[0].path.name()));
}

bool MainWindow::linkBootLoader(const Item &item, QString *err) {
    if (!hasDisc() || !writable(0)) { *err = "The disc is read-only: " + readOnlyReason(0); return false; }
    if (!m_doc.disk()->hasLoader()) { *err = "This disc has no boot loader to link: copy one in first (Tools > Copy boot loader from)."; return false; }
    const int k = m_doc.disk()->indexOf(item.path.name());
    if (k < 0) { *err = "no such file: " + item.path.name(); return false; }
    const Flex::TS to = m_doc.disk()->files()[k].start;
    const bool ok = m_doc.disc().transact([&](QString *e) { return m_doc.disk()->setBootLink(to, e); }, err);
    refresh();
    return ok;
}
