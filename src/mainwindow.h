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
// mainwindow.h - FLEX Disk Wrangler's window, on DiskWranglerCore's window kit (dw::ui::DiscWindow) over a
// Flex::OpenedDisc. The kit does the rows, the selection, the title, Save / Save As / Revert / Close, Extract,
// Properties' plumbing, Check and opening; this class fills its hooks with FLEX's words, columns and dialogs. Menus,
// the button bar and the context menu keep the family's order (CP/M Disk Wrangler's AGENTS.md, "The window").
// Series flex-1 only reads: the items that change a disc are there, greyed out, with the reason in the status line.
#pragma once

#include <diskwrangler/ui/discwindow.h>

#include "flexvolume.h"

class QAction;

// The file list: the kit's pane, dragging files out (the retro-files format, written only when a target asks), taking
// dropped disc images in a window with no FLEX disc, and files dropped on a disc that can be written.
class FlexPane : public dw::ui::VolumePane {
    Q_OBJECT
public:
    explicit FlexPane(QWidget *parent = nullptr);
    QMimeData *makeMime(const QList<QTreeWidgetItem *> &items) const override;
protected:
    DropRules dropRules() const override;
    void startDrag(Qt::DropActions supported) override;
};

class MainWindow : public dw::ui::DiscWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    static MainWindow *byId(quint64 id);

    Flex::OpenedDisc *document() const override { return const_cast<Flex::OpenedDisc *>(&m_doc); }
    dw::app::AppInfo appInfo() const override;
    bool hasDisc() const { return m_doc.disk() != nullptr; }
    // What a drag, Copy and View take from the disc.
    dw::DragSpec dragSpec() const;
    using DiscWindow::writable;
    using DiscWindow::readOnlyReason;

    void openDialog() override;
    void viewSelected() override;
    void copySelected() override;
    void about() override;
    void newImageDialog() override;
    void addFilesDialog() override;
    void paste() override;
    void renameSelected() override;

    // What the dialogs do, for them and the tests. newImage: a blank disc (Flex::createBlank) at path, its boot loader
    // copied from bootSource if one is named, opened in this window (or a new one if this one has a disc). The others
    // change the open disc in one transaction each.
    bool newImage(const QString &path, const QString &presetId, const QString &name, int number, const QString &bootSource, QString *err);
    bool renameFile(const Item &item, const QString &newName, QString *err);
    // The boot loader of another FLEX disc copied in (sectors 1 and 2), linked to FLEX.SYS if this disc has one, else
    // left unlinked; *done says which.
    bool copyBootLoader(const QString &image, QString *done, QString *err);
    // LINK: the boot loader pointed at a file's first sector.
    bool linkBootLoader(const Item &item, QString *err);
    void importPaths(const QStringList &paths);
    void importJson(const QByteArray &json);

protected:
    ActionSpec actionSpec(Act a) const override;
    QStringList paneGroups() const override { return {"flex"}; }
    dw::ui::PaneLayout paneLayout(const QString &group) const override;
    dw::ui::VolumePane *createPane(const QString &group) override;
    QList<QAction *> contextActions() const override;
    QString rootLabel(int mount) const override;
    void decorateRow(QTreeWidgetItem *row, int mount, const dw::fs::Path &path, const dw::fs::Entry *entry) const override;
    QStringList statusVariants() const override;
    void updateActions() override;
    bool keepBackup() const override;
    bool askSaveAs(QString *path, dw::device::Kind *kind) override;
    bool afterSaveAs(const QString &path, QString *err) override;
    std::shared_ptr<dw::xfer::HostPolicy> hostPolicy(const QVector<dw::TransferItem> &items) const override;
    bool propertiesEditor(const QVector<Item> &items, PropertiesEdit *edit) override;
    bool volumeEditor(int mount, const QString &key, VolumeEdit *edit) override;
    bool dropOpensFiles() const override { return !hasDisc(); }
    void droppedOnDisc(dw::ui::VolumePane *pane, QTreeWidgetItem *target, const QStringList &paths) override;
    bool detectImage(const QString &path, int force, dw::fs::Verdict *verdict) override;
    bool openDocument(const QString &path, const dw::fs::Verdict &verdict, const dw::fs::Candidate *pick, int force) override;
    void openStopped() override;
    void afterOpen(const QString &path) override;
    dw::ui::DiscWindow *createWindow() override { return makeWindow<MainWindow>(); }

private slots:
    void discInfo();
    void copyBootLoaderDialog();
    void linkBootLoaderSelected();

private:
    FlexPane *flexPane() const { return static_cast<FlexPane *>(pane()); }
    Flex::OpenedDisc m_doc;
    QString m_notFlex;                 // why the open file wasn't read as FLEX (the status line says it)
    QAction *m_actCheck = nullptr, *m_actInfo = nullptr, *m_actLabel = nullptr, *m_optBackup = nullptr;
    QAction *m_actBoot = nullptr, *m_actLink = nullptr;
};
