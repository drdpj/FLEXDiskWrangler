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
// voltest - FLEX Disk Wrangler's discs through DiskWranglerCore's filesystem interface: detection by tiers, the
// FLEX volume as a dw::fs::Disc (listing, reading, attributes, the check), changes made as FLEX makes them, each in a
// transaction, a faithful reload, Save As, and the sample discs; and UniFLEX discs, read (series uniflex-1) and
// changed (series uniflex-2); a FLEX disc in an ImageDisk file (series containers).
//   voltest <dir made by flex_check.py make and uniflex_check.py make> [<the sample FLEX discs' folder>
//           [<the UniFLEX sample discs' folder>]]
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <cstdio>
#include <functional>

#include <diskwrangler/app/clock.h>

#include "../src/flextext.h"
#include "../src/flexvolume.h"

using dw::fs::Path;

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, qPrintable(QString(what))); ++fails; } \
                            else printf("  ok: %s\n", qPrintable(QString(what))); } while (0)

static dw::fs::Verdict verdictOf(const QString &path) {
    dw::device::Container img;
    QString err;
    if (!img.open(path, &err)) return dw::fs::Verdict();
    return Flex::verdict(img, path);
}

static bool openDisc(const QString &path, Flex::OpenedDisc *od, QString *err) {
    const dw::fs::Verdict v = verdictOf(path);
    const dw::fs::Candidate *c = v.bestCandidate();
    if (!c || c->fsId != "flex") { *err = "not read as FLEX"; return false; }
    return od->open(path, std::any_cast<Flex::ProbeResult>(c->data), err);
}

static bool openUniflex(const QString &path, Flex::OpenedDisc *od, QString *err, dw::fs::Tier *tier = nullptr) {
    const dw::fs::Verdict v = verdictOf(path);
    const dw::fs::Candidate *c = v.bestCandidate();
    if (!c || c->fsId != "uniflex" || !c->supported) { *err = "not read as UniFLEX"; return false; }
    if (tier) *tier = c->tier;
    return od->open(path, std::any_cast<UniFlex::ProbeResult>(c->data), err);
}

// Everything a UniFLEX Fs holds in memory, as text: what reload() must give back exactly.
static QString describe(const UniFlex::Fs &fs) {
    QStringList out;
    for (const UniFlex::Node &n : fs.nodes()) {
        QStringList kids;
        for (int k : n.kids) kids << QString::number(k);
        out << QString("%1 %2 %3 [%4] {%5}").arg(QString::fromLatin1(n.name.toLatin1().toHex())).arg(n.fdn).arg(n.parent).arg(kids.join(','), n.problem);
    }
    for (const UniFlex::Fdn &f : fs.fdns())
        if (f.busy()) out << QString("fdn %1 %2 %3 %4 %5 %6 %7").arg(f.number).arg(f.mode).arg(f.access).arg(f.links).arg(f.owner).arg(f.size).arg(f.time);
    const UniFlex::Sir &s = fs.sir();
    out << QString("sir %1 %2 %3 %4 %5").arg(s.fdnBlocks).arg(s.lastBlock).arg(s.freeBlocks).arg(s.freeFdns).arg(s.listedFree.size());
    return out.join("\n");
}

// Everything the Disk holds in memory, as text: what reload() must give back exactly.
static QString describe(const Flex::Disk &d) {
    QStringList out;
    for (const Flex::File &f : d.files())
        out << QString("%1 %2 %3 a%4 %5-%6 n%7 r%8 %9/%10/%11 [%12]").arg(f.slot).arg(f.name, QString::fromLatin1(f.rawName.toHex()))
                   .arg(f.attr).arg(f.start.toString(), f.end.toString()).arg(f.sectors).arg(f.random).arg(f.month).arg(f.day).arg(f.year).arg(f.problem);
    const Flex::Sir &s = d.sir();
    out << QString("sir %1 %2 %3-%4 %5 %6/%7").arg(QString::fromLatin1(s.label.toHex())).arg(s.volumeNumber).arg(s.firstFree.toString(), s.lastFree.toString())
               .arg(s.freeCount).arg(s.lastTrack).arg(s.sectors);
    out << QString("dir %1 deleted %2 free %3 track0 %4").arg(d.dirSectors().size()).arg(d.deletedEntries()).arg(d.freeSlots()).arg(d.track0Sectors());
    return out.join("\n");
}

static QByteArray slurp(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }

static void detection(const QString &dir) {
    printf("detection by tiers\n");
    struct Want { const char *file; const char *fs; dw::fs::Tier tier; bool supported; dw::fs::Verdict::Action action; };
    const Want wants[] = {
        {"basic.dsk", "flex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"big.dsk", "flex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"empty.dsk", "flex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"short.dsk", "flex", dw::fs::Tier::Possible, true, dw::fs::Verdict::Open},
        {"short0.dsk", "flex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"pad0.dsk", "flex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"cut.dsk", "flex", dw::fs::Tier::Possible, true, dw::fs::Verdict::Open},
        {"os9.dsk", "os9", dw::fs::Tier::Likely, false, dw::fs::Verdict::Open},
        {"uniflex.dsk", "uniflex", dw::fs::Tier::Possible, false, dw::fs::Verdict::Open},   // a SIR naming UniFLEX, nothing more
        {"uf-basic.dsk", "uniflex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"uf-many.dsk", "uniflex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"uf-bad-chain.dsk", "uniflex", dw::fs::Tier::Certain, true, dw::fs::Verdict::Open},
        {"uf-short.dsk", "uniflex", dw::fs::Tier::Possible, true, dw::fs::Verdict::Open},
        {"uf-noroot.dsk", "uniflex", dw::fs::Tier::Possible, true, dw::fs::Verdict::Open},
    };
    for (const Want &w : wants) {
        const dw::fs::Verdict v = verdictOf(dir + "/" + w.file);
        const dw::fs::Candidate *c = v.bestCandidate();
        CHECK(c && c->fsId == w.fs && c->tier == w.tier && c->supported == w.supported && v.action == w.action,
              QString("%1: %2, %3%4").arg(w.file, w.fs, dw::fs::tierName(w.tier), w.supported ? "" : ", not supported"));
    }
    const dw::fs::Verdict v = verdictOf(dir + "/noise.bin");
    CHECK(v.action == dw::fs::Verdict::None, "noise.bin: nothing recognised");
}

static void basic(const QString &dir, const QString &work) {
    printf("basic.dsk: listing, reading, attributes\n");
    Flex::OpenedDisc od;
    QString err;
    CHECK(openDisc(dir + "/basic.dsk", &od, &err), "opened: " + err);
    if (!od.disk()) return;
    dw::fs::Volume *v = od.disc().volume(0);
    CHECK(v && v->fsId() == "flex", "one FLEX volume");
    QVector<dw::fs::Entry> es;
    CHECK(v->list(Path(), &es, &err), "listed");
    QStringList names;
    for (const auto &e : es) names << e.name;
    CHECK(names == QStringList({"README.TXT", "PROG.CMD", "DATA.RAN", "EMPTY", "HIDDEN.SYS"}), "names in directory order, the deleted entry left out: " + names.join(" "));
    CHECK(od.disk()->deletedEntries() == 1, "one deleted entry");
    CHECK(v->info().title == "BASIC" && v->volumeValues().value("number").toInt() == 42, "label BASIC, volume 42");

    QByteArray text;
    CHECK(v->read(Path({"README.TXT"}), &text, &err), "README.TXT read");
    CHECK(text.size() % Flex::DataBytes == 0 && text.contains(QByteArray("\x09\x08LDA\x09\x05#$41\r")), "kept as stored: TAB + count, CR");
    const QString shown = Flex::expandText(text);
    CHECK(shown.startsWith("* A FLEX text file\n        LDA     #$41\n        JSR     PUTCHR    print it\n"), "expanded for viewing");
    CHECK(Flex::looksLikeText(text), "looks like text");

    QByteArray prog;
    CHECK(v->read(Path({"PROG.CMD"}), &prog, &err) && prog.size() == 12 * Flex::DataBytes, "PROG.CMD: 3000 bytes in 12 sectors");
    CHECK(!Flex::looksLikeText(prog), "binary isn't text");
    QByteArray ran;
    QByteArray want;
    for (int k = 0; k < 10; ++k) for (int i = 0; i < 256; ++i) want.append(char(i));
    CHECK(v->read(Path({"DATA.RAN"}), &ran, &err) && ran.startsWith(want) && ran.size() == 11 * Flex::DataBytes,
          "DATA.RAN: the data without the two map sectors");
    dw::fs::Entry e;
    CHECK(v->stat(Path({"DATA.RAN"}), &e, &err) && e.attrs["random"].toBool() && e.attrs["sectors"].toInt() == 13 && !e.sizeExact,
          "DATA.RAN: random, 13 sectors, size known to a sector");
    CHECK(v->stat(Path({"PROG.CMD"}), &e, &err) && e.attrs["write"].toBool() && e.attrs["delete"].toBool() && !e.attrs["read"].toBool() &&
          e.modified.date() == QDate(1999, 12, 31), "PROG.CMD: write and delete protected, 31 Dec 1999");
    CHECK(v->stat(Path({"HIDDEN.SYS"}), &e, &err) && e.attrs["read"].toBool() && e.attrs["catalog"].toBool(), "HIDDEN.SYS: read and catalogue protected");
    CHECK(v->stat(Path({"README.TXT"}), &e, &err) && e.modified.date() == QDate(1984, 3, 20), "README.TXT: 20 Mar 1984");
    CHECK(v->stat(Path({"EMPTY"}), &e, &err) && !e.modified.isValid() && e.size == Flex::DataBytes, "EMPTY: no date, one sector");
    CHECK(v->check().ok() && v->check().warnings.isEmpty(), "no problems");
    const QMap<QString, QJsonObject> r = v->toRetro(es[1]);
    CHECK(r.value("flex").value("writeProtect").toBool() && r.value("flex").value("deleteProtect").toBool() && !r.value("flex").value("random").toBool(),
          "retro-files family flex");

    printf("Save As: a copy, byte for byte, read back\n");
    const QString before = describe(*od.disk());
    const QString copy = work + "/basic-copy.dsk";
    QFile::remove(copy);
    CHECK(od.saveAs(copy, dw::device::Kind::Raw, &err), "saved as a raw copy: " + err);
    CHECK(slurp(copy) == slurp(dir + "/basic.dsk"), "identical bytes");
    CHECK(!od.saveAs(work + "/basic.imd", dw::device::Kind::Imd, &err), "ImageDisk refused: " + err);
    Flex::OpenedDisc od2;
    CHECK(openDisc(copy, &od2, &err) && describe(*od2.disk()) == before, "the copy reads the same");
}

// The whole disc as staged: what a failed transaction must leave exactly as it was.
static QByteArray staged(const Flex::OpenedDisc &od) {
    QByteArray b;
    QString err;
    od.disk()->linear().read(0, od.disk()->linear().size(), &b, &err);
    return b;
}

static void changes(const QString &dir, const QString &work) {
    printf("changes, as FLEX makes them (the bytes are flex_check.py writes'; here the volume, transactions, reload, saving)\n");
    const QString path = work + "/changes.dsk";
    QFile::remove(path);
    QFile::copy(dir + "/basic.dsk", path);
    Flex::OpenedDisc od;
    QString err;
    CHECK(openDisc(path, &od, &err) && od.readOnlyWhy(0).isEmpty(), "basic.dsk opens writable: " + od.readOnlyWhy(0));
    if (!od.disk()) return;
    dw::fs::Disc &disc = od.disc();
    dw::fs::Volume *v = disc.volume(0);
    // each change in a transaction; afterwards the disc checks clean and a reload gives exactly the state left
    const auto change = [&](const QString &what, const std::function<bool(QString *)> &f) {
        QString e;
        const bool ok = disc.transact(f, &e);
        const QString after = describe(*od.disk());
        QString re;
        CHECK(ok && v->check().ok() && v->reload(&re) && describe(*od.disk()) == after, what + (ok ? QString() : ": " + e));
    };
    // a change that fails leaves the staged disc and the state as they were
    const auto refused = [&](const QString &what, const QString &want, const std::function<bool(QString *)> &f) {
        const QByteArray bytes = staged(od);
        const QString state = describe(*od.disk());
        QString e;
        const bool ok = disc.transact(f, &e);
        CHECK(!ok && e.contains(want) && staged(od) == bytes && describe(*od.disk()) == state, what + " refused: " + e);
    };
    const QByteArray note = QByteArray("a note\r").repeated(60);
    change("write NOTE.TXT", [&](QString *e) { return v->write(Path(), "note.txt", note, {}, QDateTime(QDate(1984, 3, 20), QTime(12, 0)), false, e); });
    dw::fs::Entry en;
    QByteArray back;
    CHECK(v->stat(Path({"NOTE.TXT"}), &en, &err) && en.modified.date() == QDate(1984, 3, 20) && en.attrs["sectors"].toInt() == 2,
          "NOTE.TXT: upper case on the disc, its own date, 2 sectors");
    CHECK(v->read(Path({"NOTE.TXT"}), &back, &err) && back.left(note.size()) == note && back.mid(note.size()) == QByteArray(back.size() - note.size(), '\0'),
          "read back: the data, then zeros to the end of the sector");
    CHECK(od.disk()->files()[2].name == "NOTE.TXT", "in the deleted entry's slot (the first deleted or never-used one)");
    refused("an empty file", "FLEX can't hold an empty file", [&](QString *e) { return v->write(Path(), "E.TXT", QByteArray(), {}, QDateTime(), false, e); });
    refused("a name that exists", "already exists", [&](QString *e) { return v->write(Path(), "NOTE.TXT", "x", {}, QDateTime(), false, e); });
    refused("a bad name", "starts with a letter", [&](QString *e) { return v->write(Path(), "1X.TXT", "x", {}, QDateTime(), false, e); });
    refused("more than the disc holds", "there isn't room", [&](QString *e) {
        return v->write(Path(), "HUGE.BIN", QByteArray(400 * Flex::DataBytes, 'h'), {}, QDateTime(), false, e); });
    refused("deleting a write-protected file", "write-protected", [&](QString *e) { return v->remove(Path({"PROG.CMD"}), e); });
    refused("renaming a read-protected file", "read-protected", [&](QString *e) { return v->rename(Path({"HIDDEN.SYS"}), Path({"H.SYS"}), e); });
    refused("replacing a protected file", "write-protected", [&](QString *e) { return v->write(Path(), "PROG.CMD", "x", {}, QDateTime(), true, e); });
    refused("renaming onto another file", "already exists", [&](QString *e) { return v->rename(Path({"NOTE.TXT"}), Path({"README.TXT"}), e); });
    refused("changing the random-access flag", "can't be changed", [&](QString *e) { return v->setAttrs(Path({"DATA.RAN"}), {{"random", false}}, e); });
    refused("a volume name of 9 characters", "up to 8", [&](QString *e) { return v->setVolumeAttrs({{"label", "NINECHARS"}}, e); });

    // a random file copied: its data read without the map, written with a new one, read back the same
    QByteArray ran;
    v->read(Path({"DATA.RAN"}), &ran, &err);
    change("write COPY.RAN as a random file", [&](QString *e) {
        return v->write(Path(), "COPY.RAN", ran, {{"random", true}, {"write", true}}, QDateTime(QDate(2001, 6, 1), QTime()), false, e); });
    QByteArray ran2;
    CHECK(v->read(Path({"COPY.RAN"}), &ran2, &err) && ran2 == ran && v->stat(Path({"COPY.RAN"}), &en, &err) &&
          en.attrs["random"].toBool() && en.attrs["write"].toBool() && en.attrs["sectors"].toInt() == 13 && v->check().warnings.isEmpty(),
          "COPY.RAN: the same data, random, write-protected, 13 sectors, its map lists its chain");
    const Flex::File readme = od.disk()->files()[od.disk()->indexOf("README.TXT")];
    const int freeBefore = od.disk()->sir().freeCount;
    change("delete README.TXT", [&](QString *e) { return v->remove(Path({"README.TXT"}), e); });
    CHECK(od.disk()->sir().lastFree == readme.end && od.disk()->sir().freeCount == freeBefore + readme.sectors,
          QString("its chain is at the end of the free chain: the last free sector is its last, %1, and %2 more are free")
              .arg(readme.end.toString()).arg(readme.sectors));
    change("rename NOTE.TXT to LETTER", [&](QString *e) { return v->rename(Path({"NOTE.TXT"}), Path({"letter"}), e); });
    CHECK(od.disk()->indexOf("LETTER") >= 0 && od.disk()->indexOf("NOTE.TXT") < 0, "renamed, upper case, no extension");
    change("protect LETTER (catalogue, delete)", [&](QString *e) { return v->setAttrs(Path({"LETTER"}), {{"catalog", true}, {"delete", true}}, e); });
    CHECK(v->stat(Path({"LETTER"}), &en, &err) && en.attrs["catalog"].toBool() && en.attrs["delete"].toBool() && !en.attrs["write"].toBool(),
          "LETTER: C and D set");
    change("unprotect PROG.CMD", [&](QString *e) { return v->setAttrs(Path({"PROG.CMD"}), {{"write", false}, {"delete", false}}, e); });
    change("replace PROG.CMD", [&](QString *e) { return v->write(Path(), "PROG.CMD", "new", {}, QDateTime(QDate(2026, 11, 15), QTime()), true, e); });
    CHECK(v->stat(Path({"PROG.CMD"}), &en, &err) && en.attrs["sectors"].toInt() == 1 && !en.attrs["write"].toBool(), "PROG.CMD replaced: one sector");
    const QByteArray tag = staged(od).mid(2 * 256 + 0x18, 3);
    change("volume name and number", [&](QString *e) { return v->setVolumeAttrs({{"label", "Disc 2"}, {"number", 7}}, e); });
    CHECK(v->volumeValues().value("label").toString() == "Disc 2" && v->volumeValues().value("number").toInt() == 7 &&
          staged(od).mid(2 * 256 + 0x18, 3) == tag, "name \"Disc 2\" as typed, number 7, SIR $18-$1A kept");

    printf("saving: the checked save, read back\n");
    const QString state = describe(*od.disk());
    CHECK(od.save(false, &err), "saved: " + err);
    Flex::OpenedDisc od2;
    CHECK(openDisc(path, &od2, &err) && describe(*od2.disk()) == state && od2.disc().volume(0)->check().ok(), "reopened: the same state, no errors");

    printf("the directory grows (full.dsk)\n");
    QFile::remove(work + "/full.dsk");
    QFile::copy(dir + "/full.dsk", work + "/full.dsk");
    Flex::OpenedDisc of;
    CHECK(openDisc(work + "/full.dsk", &of, &err) && of.disk()->freeSlots() == 0, "full.dsk: no free slot");
    if (of.disk()) {
        const int dirs = of.disk()->dirSectors().size();
        QString e;
        CHECK(of.disc().transact([&](QString *e2) { return of.disc().volume(0)->write(Path(), "MORE.TXT", "more", {}, QDateTime(), false, e2); }, &e) &&
              of.disk()->dirSectors().size() == dirs + 1 && of.disk()->freeSlots() == 9 && of.disc().volume(0)->check().ok(),
              "a new file: one more directory sector, 9 slots free in it: " + e);
    }
}

static void newDiscs(const QString &dir, const QString &work) {
    printf("new discs: every preset, as NEWDISK lays a disc out (the bytes are flex_check.py writes')\n");
    QString err;
    for (const dw::fs::Preset &p : Flex::presets()) {
        const QString path = work + "/" + p.id + ".dsk";
        QFile::remove(path);
        const int tracks = p.extra.value("tracks").toInt(), sectors = p.extra.value("sectors").toInt();
        const int track0 = p.extra.value("track0").toInt();
        CHECK(Flex::createBlank(path, p, "NEW", 9, QDate(2026, 11, 15), QByteArray(), &err), p.id + " made: " + err);
        const dw::fs::Verdict vd = verdictOf(path);
        Flex::OpenedDisc od;
        if (p.fsId == "uniflex") {           // TSC's UniFLEX formats (the bytes are uniflex_check.py writes')
            dw::fs::Tier tier = dw::fs::Tier::Possible;
            CHECK(QFileInfo(path).size() == qint64(p.bytes) && openUniflex(path, &od, &err, &tier) && tier == dw::fs::Tier::Certain &&
                  od.readOnlyWhy(0).isEmpty() && od.uniflex()->sir().packNameText() == "NEW" && od.uniflex()->sir().number == 9 &&
                  od.uniflex()->check().ok() && od.uniflex()->sir().fdnBlocks == p.bytes / UniFlex::BlockSize / 33,
                  p.id + ": UniFLEX, certain, writable, pack NEW number 9, clean, a 33rd for fdns: " + err);
            continue;
        }
        CHECK(QFileInfo(path).size() == qint64(p.bytes) && vd.bestCandidate() && vd.bestCandidate()->tier == dw::fs::Tier::Certain &&
              openDisc(path, &od, &err) && od.readOnlyWhy(0).isEmpty(), p.id + ": FLEX, certain, writable");
        if (!od.disk()) continue;
        const Flex::Sir &s = od.disk()->sir();
        CHECK(s.labelText() == "NEW" && s.volumeNumber == 9 && s.date() == QDate(2026, 11, 15) && s.freeCount == (tracks - 1) * sectors &&
              s.firstFree == Flex::TS(1, 1) && s.lastFree == Flex::TS(tracks - 1, sectors) && od.disk()->dirSectors().size() == track0 - 4 &&
              od.disk()->dirSectors().last() == Flex::TS(0, track0) && od.disk()->track0Sectors() == sectors &&
              od.disk()->files().isEmpty() && !od.disk()->hasLoader(),
              p.id + QString(": SIR, free chain, empty directory to 0/%1 (the image padded), no loader").arg(track0));
        const dw::fs::Report r = od.disc().volume(0)->check();
        CHECK(r.ok() && r.warnings.isEmpty(), p.id + ": no problems");
    }
    const dw::fs::Preset *p = Flex::findPreset("flex-40x10");
    CHECK(p && !Flex::findPreset("flex40") && !Flex::findPreset("dfs-ss40"), "presets found by id, only FLEX's");
    CHECK(!Flex::createBlank(work + "/x.dsk", *p, "TOO LONG NAME", 0, QDate(), QByteArray(), &err), "a name over 8 characters refused: " + err);

    printf("the boot loader: copied from another disc, linked\n");
    // a disc with a "loader": a new disc whose sectors 1 and 2 are given some bytes, linked to 3/4
    const QString withLoader = work + "/loader.dsk";
    QFile::remove(withLoader);
    QByteArray loader;
    for (int i = 0; i < 512; ++i) loader.append(char(i * 7 + 1));
    Flex::OpenedDisc src;
    if (Flex::createBlank(withLoader, *p, "SYSTEM", 1, QDate(), QByteArray(), &err) && openDisc(withLoader, &src, &err)) {
        CHECK(src.disc().transact([&](QString *e) { return src.disk()->setBootSectors(loader, Flex::TS(3, 4), e); }, &err) &&
              src.save(false, &err) && src.disk()->bootLink() == Flex::TS(3, 4), "a loader put on loader.dsk, linked to 3/4");
    }
    QByteArray got;
    CHECK(Flex::readBootLoader(withLoader, &got, &err) && got.size() == 512 && got.mid(7) == loader.mid(7) && got.left(5) == loader.left(5),
          "read from loader.dsk: sectors 1 and 2 (bytes 5-6 its link)");
    CHECK(!Flex::readBootLoader(dir + "/basic.dsk", &got, &err) && err.contains("no boot loader"), "basic.dsk has none: " + err);
    CHECK(!Flex::readBootLoader(dir + "/os9.dsk", &got, &err) && err.contains("isn't a FLEX disc"), "os9.dsk isn't FLEX: " + err);
    QByteArray boot;
    Flex::readBootLoader(withLoader, &boot, &err);
    const QString made = work + "/booted.dsk";
    QFile::remove(made);
    Flex::OpenedDisc od;
    CHECK(Flex::createBlank(made, *p, "BOOT", 2, QDate(), boot, &err) && openDisc(made, &od, &err) && od.disk()->hasLoader() &&
          od.disk()->bootLink().isNull(), "a new disc with that loader, not linked");
    if (od.disk()) {
        QByteArray b;
        od.disk()->bootSectors(&b, &err);
        CHECK(b.mid(7) == loader.mid(7) && b.left(5) == loader.left(5), "the loader's bytes copied as they are");
        CHECK(od.disc().transact([&](QString *e) { return od.disc().volume(0)->write(Path(), "FLEX.SYS", QByteArray(3000, 's'), {}, QDateTime(), false, e)
                                                       && od.disk()->setBootLink(od.disk()->files()[0].start, e); }, &err) &&
              od.disk()->bootLink() == Flex::TS(1, 1), "FLEX.SYS added and linked: the loader points at 1/1");
    }
}

// A double-density disc's single-density track 0 (the Adaptation Guide, Appendix B): stored at its own size (short0.dsk,
// 77 x 26 with a track 0 of 15), padded to the full track (pad0.dsk, as the sample EFORTH), and a full track 0 in a file
// whose size happens to be a short one's (cut.dsk).
static void track0(const QString &dir, const QString &work) {
    printf("track 0 single density: stored short, padded, and a file that only looks short\n");
    const QString path = work + "/short0.dsk";
    QFile::remove(path);
    QFile::copy(dir + "/short0.dsk", path);
    const qint64 size = QFileInfo(path).size();
    Flex::OpenedDisc od;
    QString err;
    CHECK(openDisc(path, &od, &err) && od.readOnlyWhy(0).isEmpty(), "short0.dsk opens writable: " + err + od.readOnlyWhy(0));
    if (!od.disk()) return;
    const Flex::Disk &d = *od.disk();
    CHECK(d.shortTrack0() && d.track0Sectors() == 15 && d.sectorsPerTrack() == 26 && d.inRange(Flex::TS(0, 15)) &&
          !d.inRange(Flex::TS(0, 16)) && d.inRange(Flex::TS(1, 26)), "track 0 of 15 sectors, the others 26");
    CHECK(d.files().size() == 110 && d.dirSectors().size() == 11 && d.freeSlots() == 0, "110 files, the directory 0/5 to 0/15, full");
    const dw::fs::Report r = od.disc().volume(0)->check();
    CHECK(r.ok() && r.warnings.isEmpty(), "no problems: " + r.errors.join("; ") + r.warnings.join("; "));
    dw::fs::Volume *v = od.disc().volume(0);
    QString e;
    const bool ok = od.disc().transact([&](QString *e2) { return v->write(Path(), "GROW.TXT", "grows", {}, QDateTime(), false, e2); }, &e);
    const QString after = describe(d);
    QString re;
    CHECK(ok && d.dirSectors().size() == 12 && d.dirSectors().last().t >= 1 && v->check().ok() && v->reload(&re) && describe(d) == after,
          "a new file: the directory grows onto track 1; reload faithful: " + e);
    CHECK(od.save(false, &err) && QFileInfo(path).size() == size, "saved, track 0 still stored short: " + err);
    Flex::OpenedDisc again;
    CHECK(openDisc(path, &again, &err) && again.disk()->track0Sectors() == 15 && describe(*again.disk()) == after, "read back the same");
    const QString copy = work + "/short0-copy.dsk";
    QFile::remove(copy);
    Flex::OpenedDisc c2;
    CHECK(od.saveAs(copy, dw::device::Kind::Raw, &err) && QFileInfo(copy).size() == size && openDisc(copy, &c2, &err) &&
          describe(*c2.disk()) == after, "Save As: the same layout: " + err);

    Flex::OpenedDisc pd;
    CHECK(openDisc(dir + "/pad0.dsk", &pd, &err) && pd.readOnlyWhy(0).isEmpty() && pd.disk()->track0Sectors() == 18 &&
          pd.disk()->dirSectors().size() == 6 && pd.disk()->files().size() == 2 && pd.disc().volume(0)->check().ok(),
          "pad0.dsk: track 0 padded, the directory 0/5 to 0/10, writable");
    Flex::OpenedDisc cd;
    CHECK(openDisc(dir + "/cut.dsk", &cd, &err) && !cd.disk()->shortTrack0() && cd.disk()->track0Sectors() == 26 && !cd.readOnlyWhy(0).isEmpty(),
          "cut.dsk: 0/15 links on to 0/16, so a full track 0 in a short file, read-only: " + cd.readOnlyWhy(0));
}

// A FLEX disc in an ImageDisk file: short0.dsk on its 8" disc (double density, 26 sectors, MFM at 500 kbit/s) with
// its single-density track 0 (15 sectors, FM). It reads as the raw file does, changes and saves into the ImageDisk
// file keeping each track's shape, and Save As follows its rule (a raw image raw only).
static void containers(const QString &dir, const QString &work) {
    printf("a FLEX disc in an ImageDisk file, track 0 single density\n");
    using dw::device::Kind;
    QString err;
    const QByteArray raw = slurp(dir + "/short0.dsk");
    dw::device::RawLayout l;
    l.seclen = 256; l.sectrk = 26; l.cyls = 77; l.heads = 1; l.mode = 3;
    dw::device::Container m;
    m.createBlank(Kind::Imd, l, "voltest");
    CHECK(m.reshapeTrack(0, 0, 15, 256, 1, 0, &err), "track 0: 15 FM sectors");
    m.setOwnTrack0(true);
    dw::device::Linear lin(&m);
    const QString imd = work + "/short0.imd";
    QFile::remove(imd);
    CHECK(lin.size() == raw.size() && lin.write(0, raw, &err) && m.saveAs(imd, Kind::Imd, &err), "made: " + err);
    const dw::fs::Verdict v = verdictOf(imd);
    CHECK(v.bestCandidate() && v.bestCandidate()->fsId == "flex" && v.bestCandidate()->tier == dw::fs::Tier::Certain &&
          v.bestCandidate()->description.endsWith("(track 0: 15) in an ImageDisk file"), "certain: " + (v.bestCandidate() ? v.bestCandidate()->description : QString()));
    Flex::OpenedDisc od, rd;
    CHECK(openDisc(imd, &od, &err) && od.readOnlyWhy(0).isEmpty() && od.container().kind() == Kind::Imd, "opens writable: " + err + od.readOnlyWhy(0));
    CHECK(openDisc(dir + "/short0.dsk", &rd, &err), "and the raw file");
    if (!od.disk() || !rd.disk()) return;
    CHECK(describe(*od.disk()) == describe(*rd.disk()) && od.disk()->track0Sectors() == 15, "the same disc as the raw file");
    for (Flex::OpenedDisc *x : {&od, &rd}) {
        dw::fs::Volume *vol = x->disc().volume(0);
        CHECK(x->disc().transact([&](QString *e) { return vol->write(Path(), "GROW.TXT", "grows", {}, QDateTime(), false, e); }, &err), "a new file: " + err);
    }
    QByteArray a, b;
    CHECK(od.disk()->linear().read(0, raw.size(), &a, &err) && rd.disk()->linear().read(0, raw.size(), &b, &err) && a == b,
          "the same change as on the raw file, the same bytes");
    const QString after = describe(*od.disk());
    CHECK(od.save(false, &err) && od.container().kind() == Kind::Imd && od.container().ownTrack0(), "saved into the ImageDisk file: " + err);
    Flex::OpenedDisc again;
    CHECK(openDisc(imd, &again, &err) && describe(*again.disk()) == after, "read back the same: " + err);
    const dw::device::Track *t0 = again.container().track(0, 0), *t1 = again.container().track(1, 0);
    CHECK(t0 && t0->mode == 0 && t0->secs.size() == 15 && t1 && t1->mode == 3 && t1->secs.size() == 26, "each track's density and sectors kept");

    const QString asRaw = work + "/short0-from-imd.dsk", asDsk = work + "/short0-copy.dsk", no = work + "/no.imd";
    for (const QString &f : {asRaw, asDsk, no}) QFile::remove(f);
    Flex::OpenedDisc c1, c2;
    CHECK(again.convertTo(asRaw, Kind::Raw, &err) && slurp(asRaw) == a && openDisc(asRaw, &c1, &err) && c1.disk()->track0Sectors() == 15 &&
          describe(*c1.disk()) == after, "converted to raw: track 0 stored short, the same disc: " + err);
    CHECK(again.saveAs(asDsk, Kind::Edsk, &err) && again.container().kind() == Kind::Edsk && openDisc(asDsk, &c2, &err) &&
          describe(*c2.disk()) == after, "Save As extended DSK: " + err);
    CHECK(!rd.canSaveAs(Kind::Imd, &err) && err.contains("only be saved as a raw image") && !rd.saveAs(no, Kind::Imd, &err) && !QFile::exists(no),
          "a raw image isn't saved as ImageDisk: " + err);
    CHECK(!again.canSaveAs(Kind::Td0, &err) && rd.canSaveAs(Kind::Raw, &err), "nor anything as TeleDisk");
}

static void faults(const QString &dir) {
    printf("damaged discs: check() names the fault, and the disc can't be changed\n");
    const QStringList want = {"bad-freeloop.dsk:the free chain loops back", "bad-freecount.dsk:the SIR says 322 sectors are free",
                              "bad-chain.dsk:ONE.TXT: its chain ends after 3", "bad-crosslink.dsk:belongs to both ONE.TXT and the free chain",
                              "bad-dirloop.dsk:the directory's chain loops back to 0/5"};
    for (const QString &w : want) {
        const QString file = w.section(':', 0, 0), text = w.section(':', 1);
        Flex::OpenedDisc od;
        QString err;
        CHECK(openDisc(dir + "/" + file, &od, &err), file + " opened: " + err);
        if (!od.disk()) continue;
        const dw::fs::Report r = od.disc().volume(0)->check();
        CHECK(!r.ok() && r.errors.join("\n").contains(text), file + ": " + r.errors.value(0));
        CHECK(od.readOnlyWhy(0) == "the disc has errors (Tools > Check disc)", file + ": read-only for its errors");
    }
    Flex::OpenedDisc os;
    QString err;
    CHECK(openDisc(dir + "/short.dsk", &os, &err) && (!os.disc().volume(0)->check().ok() || os.readOnlyWhy(0) == Flex::OpenedDisc::GeometryText),
          "short.dsk (shorter than its SIR says): read-only: " + os.readOnlyWhy(0));
    Flex::OpenedDisc od;
    CHECK(openDisc(dir + "/bad-chain.dsk", &od, &err), "bad-chain.dsk opened");
    if (!od.disk()) return;
    QVector<dw::fs::Entry> es;
    od.disc().volume(0)->list(Path(), &es, &err);
    CHECK(es.size() == 2 && !es[0].problem.isEmpty() && es[1].problem.isEmpty(), "ONE.TXT listed with its problem, TWO.BIN without");
    QByteArray data;
    CHECK(!od.disc().volume(0)->read(Path({"ONE.TXT"}), &data, &err) && err.contains("its chain ends"), "ONE.TXT can't be read: " + err);
    CHECK(od.disc().volume(0)->read(Path({"TWO.BIN"}), &data, &err), "TWO.BIN can");
}

static void names() {
    printf("names\n");
    Flex::NameRules n;
    QString why;
    CHECK(n.valid("README.TXT", dw::fs::NodeKind::File, &why) && n.valid("A", dw::fs::NodeKind::File, &why) &&
          n.valid("X-1_2.B", dw::fs::NodeKind::File, &why), "valid names");
    CHECK(!n.valid("1ABC", dw::fs::NodeKind::File, &why), "a name starts with a letter: " + why);
    CHECK(!n.valid("TOOLONGNAME", dw::fs::NodeKind::File, &why), "8 characters at most: " + why);
    CHECK(!n.valid("A.LONG", dw::fs::NodeKind::File, &why), "3 in the extension: " + why);
    CHECK(!n.valid("A B", dw::fs::NodeKind::File, &why), "no spaces: " + why);
    CHECK(n.fromHost("my file.text", dw::fs::NodeKind::File) == "MY_FILE.TEX", "from a host name: " + n.fromHost("my file.text", dw::fs::NodeKind::File));
    CHECK(n.fromHost("2nd.c", dw::fs::NodeKind::File) == "F2ND.C", "a leading digit gets a letter: " + n.fromHost("2nd.c", dw::fs::NodeKind::File));
}

static void samples(const QString &dir) {
    printf("the sample discs (%s)\n", qPrintable(dir));
    const QStringList damaged = {"C009.DSK", "FLX503TX.DSK", "FLEX2-01.dsk"};
    const QStringList os9 = {"C022.DSK", "C022_1.DSK"};
    const QStringList uniflex = {"C012_1.DSK", "C014.DSK", "C014.DSK.CORRUPTED"};
    int flex = 0;
    for (const QString &f : QDir(dir).entryList(QDir::Files, QDir::Name)) {
        const QString p = dir + "/" + f;
        const dw::fs::Verdict v = verdictOf(p);
        const dw::fs::Candidate *c = v.bestCandidate();
        if (os9.contains(f)) {
            CHECK(c && !c->supported && c->fsId == "os9", f + ": " + (c ? c->description : QString("nothing")));
            continue;
        }
        if (uniflex.contains(f)) {
            // C012_1 is a block short of its swap space (likely); C014 is exact; both have errors (uniflex_samples.txt)
            Flex::OpenedDisc od;
            QString err;
            dw::fs::Tier tier;
            CHECK(openUniflex(p, &od, &err, &tier) && tier == (f == "C012_1.DSK" ? dw::fs::Tier::Likely : dw::fs::Tier::Certain) &&
                  !od.disc().volume(0)->check().ok() && !od.readOnlyWhy(0).isEmpty(), f + ": UniFLEX, read-only, its faults found: " + err);
            continue;
        }
        Flex::OpenedDisc od;
        QString err;
        CHECK(openDisc(p, &od, &err) && c->tier == dw::fs::Tier::Certain, f + ": FLEX, certain");
        if (!od.disk()) continue;
        flex++;
        const dw::fs::Report r = od.disc().volume(0)->check();
        if (damaged.contains(f)) {
            CHECK(!r.ok() && !od.readOnlyWhy(0).isEmpty(), f + ": read-only for its errors: " + r.errors.value(0));
            continue;
        }
        CHECK(r.ok() && od.readOnlyWhy(0).isEmpty(), f + ": no errors, writable");
        const QString before = describe(*od.disk());
        CHECK(od.disk()->reload(&err) && describe(*od.disk()) == before, f + ": a reload gives the same state");
        // a change on a copy: a file added and the first unprotected one deleted, checked, saved and read back
        const QString copy = QDir::temp().filePath("flexvoltest/sample-" + f);
        QFile::remove(copy);
        QFile::copy(p, copy);
        QFile(copy).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        Flex::OpenedDisc oc;
        if (!openDisc(copy, &oc, &err)) continue;
        dw::fs::Volume *ov = oc.disc().volume(0);
        QString victim;
        for (const Flex::File &k : oc.disk()->files()) if (!(k.attr & 0xE0)) { victim = k.name; break; }
        const bool ok = oc.disc().transact([&](QString *e) {
            return ov->write(Path(), "ADDED.TXT", QByteArray("added by voltest\r").repeated(50), {}, QDateTime(QDate(2026, 11, 15), QTime()), false, e) &&
                   (victim.isEmpty() || ov->remove(Path({victim}), e));
        }, &err);
        const QString state = describe(*oc.disk());
        Flex::OpenedDisc again;
        CHECK(ok && ov->check().ok() && oc.save(false, &err) && openDisc(copy, &again, &err) && describe(*again.disk()) == state,
              f + ": ADDED.TXT added, " + victim + " deleted, saved and read back: " + err);
        QFile::remove(copy);
    }
    CHECK(flex == 17, QString("%1 FLEX discs").arg(flex));
}

// ------------------------------------------------------------------------------------------------ UniFLEX

static QStringList namesIn(dw::fs::Volume *v, const Path &dir) {
    QVector<dw::fs::Entry> es;
    QString err;
    QStringList n;
    if (v->list(dir, &es, &err)) for (const dw::fs::Entry &e : es) n << e.name + (e.isDir() ? "/" : "");
    n.sort();
    return n;
}

static QByteArray crText(const QStringList &lines) {
    QByteArray b;
    for (const QString &l : lines) b += l.toLatin1() + '\r';
    return b;
}

// uf-basic.dsk (uniflex_check.py make): the tree, every kind of file, the attributes, the refusals, reload, Save As
// The whole UniFLEX disc as staged.
static QByteArray staged(const UniFlex::Fs &fs) {
    QByteArray b;
    QString err;
    fs.linear().read(0, fs.linear().size(), &b, &err);
    return b;
}

// Every kind of change on a copy of uf-basic.dsk, each in a transaction: the disc checks clean afterwards and a reload
// gives exactly the state left; a refused change leaves the staged disc and the state as they were; then the save.
static void uniflexChanges(Flex::OpenedDisc &od, const QString &path) {
    using namespace UniFlex;
    dw::fs::Disc &disc = od.disc();
    dw::fs::Volume *v = disc.volume(0);
    const Fs &fs = *od.uniflex();
    QString err;
    const QDateTime now(QDate(2026, 11, 16), QTime(12, 0), QTimeZone::utc());
    dw::setNow(now);
    const auto change = [&](const QString &what, const std::function<bool(QString *)> &f) {
        QString e;
        const bool ok = disc.transact(f, &e);
        const QString after = describe(fs);
        QString re;
        const dw::fs::Report r = v->check();
        CHECK(ok && r.ok() && v->reload(&re) && describe(fs) == after, what + (ok ? QString() : ": " + e) + r.errors.join("; "));
    };
    const auto refused = [&](const QString &what, const QString &want, const std::function<bool(QString *)> &f) {
        const QByteArray bytes = staged(fs);
        const QString state = describe(fs);
        QString e;
        const bool ok = disc.transact(f, &e);
        CHECK(!ok && e.contains(want) && staged(fs) == bytes && describe(fs) == state, what + " refused: " + e);
    };
    const QByteArray text = QByteArray("a line\r").repeated(100);
    change("write a file", [&](QString *e) { return v->write(Path(), "new.txt", text, {}, QDateTime(QDate(1984, 1, 1), QTime(0, 0)), false, e); });
    dw::fs::Entry en;
    QByteArray back;
    CHECK(v->stat(Path({"new.txt"}), &en, &err) && en.modified == now && en.attrs.value("ownerRead").toBool() &&
          en.attrs.value("ownerWrite").toBool() && en.attrs.value("othersRead").toBool() && en.attrs.value("othersWrite").toBool() &&
          !en.attrs.value("ownerExecute").toBool() &&
          en.attrs.value("owner").toInt() == 0 && en.attrs.value("links").toInt() == 1,
          "new.txt: the time of the change (not the file's own), rw-rw- as UniFLEX's shell gives, owner 0, 1 link");
    CHECK(v->read(Path({"new.txt"}), &back, &err) && back == text, "read back exactly");
    change("an empty file", [&](QString *e) { return v->write(Path(), "nothing", QByteArray(), {}, QDateTime(), false, e); });
    change("mkdir", [&](QString *e) { return v->mkdir(Path(), "d", e); });
    CHECK(v->stat(Path({"d"}), &en, &err) && en.isDir() && en.attrs.value("links").toInt() == 2 && en.attrs.value("ownerWrite").toBool() &&
          en.attrs.value("othersExecute").toBool(), "d: a directory of 2 links, rwxrwx");
    change("a file brought with its access and owner",
           [&](QString *e) { return v->write(Path({"d"}), "prog", QByteArray(6000, 'p'), {{"ownerRead", true}, {"ownerExecute", true}, {"owner", 9}}, QDateTime(), false, e); });
    CHECK(v->stat(Path({"d", "prog"}), &en, &err) && en.attrs.value("ownerExecute").toBool() && en.attrs.value("owner").toInt() == 9 &&
          en.size == 6000, "d/prog: its access and owner kept");
    change("rename in place", [&](QString *e) { return v->rename(Path({"readme"}), Path({"README"}), e); });
    change("move a file", [&](QString *e) { return v->rename(Path({"new.txt"}), Path({"d", "moved.txt"}), e); });
    change("rename a directory in place", [&](QString *e) { return v->rename(Path({"d"}), Path({"dir"}), e); });
    CHECK(v->read(Path({"dir", "moved.txt"}), &back, &err) && back == text, "moved and renamed: still read");
    change("access and owner", [&](QString *e) { return v->setAttrs(Path({"Prog"}), {{"othersWrite", true}, {"owner", 4}}, e); });
    CHECK(v->stat(Path({"Prog"}), &en, &err) && en.attrs.value("othersWrite").toBool() && en.attrs.value("owner").toInt() == 4, "Prog: others write, owner 4");
    change("the SIR's names", [&](QString *e) { return v->setVolumeAttrs({{"label", "packed"}, {"fsName", "fsys"}, {"number", 9}}, e); });
    CHECK(v->info().title == "packed" && v->volumeValues().value("fsName").toString() == "fsys" && v->volumeValues().value("number").toInt() == 9,
          "pack packed, file system fsys, number 9");
    const qint64 freeBefore = fs.sir().freeBlocks;
    change("remove a tree", [&](QString *e) { return v->remove(Path({"src"}), e); });
    CHECK(fs.find({"src"}) < 0 && fs.sir().freeBlocks > freeBefore, "src and everything in it gone, its blocks free");
    change("replace a file", [&](QString *e) { return v->write(Path({"dir"}), "prog", "short", {}, QDateTime(), true, e); });
    CHECK(v->stat(Path({"dir", "prog"}), &en, &err) && en.size == 5 && en.attrs.value("owner").toInt() == 9 && en.attrs.value("ownerExecute").toBool(),
          "replaced: the new data, the fdn's access and owner kept");
    refused("a name that exists", "already exists", [&](QString *e) { return v->write(Path(), "README", "x", {}, QDateTime(), false, e); });
    refused("a file over a directory", "is a directory", [&](QString *e) { return v->write(Path(), "dir", "x", {}, QDateTime(), true, e); });
    refused("a name too long", "up to 14", [&](QString *e) { return v->write(Path(), "fifteen_chars.x", "x", {}, QDateTime(), false, e); });
    change("move a directory", [&](QString *e) { return v->rename(Path({"dir"}), Path({"bin", "dir"}), e); });
    CHECK(v->read(Path({"bin", "dir", "moved.txt"}), &back, &err) && back == text && v->stat(Path({"bin"}), &en, &err) &&
          en.attrs.value("links").toInt() == 3, "bin/dir: its files read, bin's links 3 (its own, ., dir's ..)");
    refused("a directory into itself", "into itself", [&](QString *e) { return v->rename(Path({"bin"}), Path({"bin", "dir", "bin"}), e); });
    change("move it back", [&](QString *e) { return v->rename(Path({"bin", "dir"}), Path({"dir"}), e); });
    refused("removing /.badblocks", "bad blocks", [&](QString *e) { return v->remove(Path({".badblocks"}), e); });
    refused("more than the disc holds", "full", [&](QString *e) { return v->write(Path(), "huge", QByteArray(700 * BlockSize, 'h'), {}, QDateTime(), false, e); });
    refused("an owner out of range", "0 to 65535", [&](QString *e) { return v->setAttrs(Path({"README"}), {{"owner", 70000}}, e); });
    // saved, then read again from the file
    const QString state = describe(fs);
    CHECK(od.save(false, &err), "saved: " + err);
    Flex::OpenedDisc again;
    CHECK(openUniflex(path, &again, &err) && describe(*again.uniflex()) == state && again.disc().volume(0)->check().ok(), "the saved disc reads the same: " + err);
    dw::setNow(QDateTime());
}

static void uniflexBasic(const QString &dir, const QString &work) {
    printf("UniFLEX: uf-basic.dsk\n");
    Flex::OpenedDisc od;
    QString err;
    CHECK(openUniflex(dir + "/uf-basic.dsk", &od, &err), "opened: " + err);
    if (!od.uniflex()) return;
    CHECK(!od.disk() && !od.volume(), "no FLEX disc in it");
    dw::fs::Volume *v = od.disc().volume(0);
    CHECK(v && v->fsId() == "uniflex" && v->caps().dirs && od.disc().mounts()[0].group == "uniflex", "one UniFLEX volume, with directories, in the uniflex pane");
    CHECK(namesIn(v, Path()) == QStringList({".badblocks", "Prog", "bin/", "dev/", "empty", "fourteen_chars", "readme", "src/"}),
          "the root: " + namesIn(v, Path()).join(" "));
    CHECK(namesIn(v, Path({"src"})) == QStringList({"also.c", "big.dat", "holey", "sub/"}), "/src: " + namesIn(v, Path({"src"})).join(" "));
    CHECK(namesIn(v, Path({"src", "sub"})) == QStringList({"deep.c"}), "/src/sub");
    QByteArray data;
    CHECK(v->read(Path({"readme"}), &data, &err) && data == crText({"UniFLEX test disc", "\tindented with a TAB", "last line"}),
          "readme as stored: CR line ends, a TAB");
    CHECK(v->read(Path({"src", "holey"}), &data, &err) && data == QByteArray(512, 'A') + QByteArray(1024, '\0') + QByteArray(100, 'B'),
          "a file with two blocks never written reads zeros there");
    QByteArray big(160 * 512 + 33, '\0');
    for (int i = 0; i < big.size(); ++i) big[i] = char((i * 7) & 0xFF);
    CHECK(v->read(Path({"src", "big.dat"}), &data, &err) && data == big, "big.dat: direct, single and double indirect blocks");
    CHECK(v->read(Path({"empty"}), &data, &err) && data.isEmpty(), "an empty file");
    CHECK(v->read(Path({"dev", "tty0"}), &data, &err) && data.isEmpty(), "a device has no data");
    CHECK(!v->read(Path({"src"}), &data, &err), "a directory isn't read: " + err);
    CHECK(!v->read(Path({"nothing"}), &data, &err), "no such file: " + err);
    dw::fs::Entry e;
    CHECK(v->stat(Path({"Prog"}), &e, &err) && e.size == 768 && e.attrs.value("ownerRead").toBool() && e.attrs.value("ownerWrite").toBool() &&
          e.attrs.value("ownerExecute").toBool() && e.attrs.value("othersRead").toBool() && !e.attrs.value("othersWrite").toBool() &&
          e.attrs.value("othersExecute").toBool() && e.attrs.value("owner").toInt() == 3 && e.attrs.value("type").toString() == "file",
          "Prog: rwxr-x, owner 3");
    CHECK(v->stat(Path({"bin", "hello"}), &e, &err) && e.attrs.value("setUserId").toBool(), "bin/hello sets the user id");
    CHECK(v->stat(Path({"readme"}), &e, &err) && e.modified == QDateTime(QDate(1985, 4, 25), QTime(19, 23, 40), QTimeZone::utc()),
          "readme's time, seconds since 1980 (UTC): " + e.modified.toString(Qt::ISODate));
    CHECK(v->stat(Path({"dev", "tty0"}), &e, &err) && e.attrs.value("type").toString() == "character device 3, 1" && e.size == 0,
          "dev/tty0: " + e.attrs.value("type").toString());
    CHECK(v->stat(Path({"dev", "fd0"}), &e, &err) && e.attrs.value("type").toString() == "block device 1, 0", "dev/fd0: " + e.attrs.value("type").toString());
    dw::fs::Entry a, b;
    CHECK(v->stat(Path({"src", "also.c"}), &a, &err) && v->stat(Path({"src", "sub", "deep.c"}), &b, &err) && a.attrs.value("fdn") == b.attrs.value("fdn") &&
          a.attrs.value("links").toInt() == 2, "a hard link: two names, one fdn, 2 links");
    CHECK(v->stat(Path({"src"}), &e, &err) && e.isDir() && e.attrs.value("links").toInt() == 3, "/src: 3 links (its name, its ., sub's ..)");
    const QMap<QString, QJsonObject> retro = v->toRetro(a);
    CHECK(retro.contains("uniflex") && retro["uniflex"].value("ownerRead").toBool() && retro["uniflex"].contains("owner") &&
          !retro["uniflex"].contains("fdn"), "retro-files: the access bits and owner travel, the fdn doesn't");
    const dw::fs::Report r = v->check();
    CHECK(r.ok() && r.warnings.isEmpty(), "the check is clean: " + r.errors.join("; ") + r.warnings.join("; "));
    CHECK(v->info().title == "basic" && v->volumeValues().value("fsName").toString() == "uniflex" && v->volumeValues().value("number").toInt() == 7,
          "pack basic, file system uniflex, number 7");

    // changes, as UniFLEX makes them (the bytes are uniflex_check.py writes'; here the volume, transactions, reload,
    // saving). On a copy: the reading checks above and below use uf-basic.dsk as made.
    CHECK(od.readOnlyWhy(0).isEmpty(), "uf-basic.dsk opens writable: " + od.readOnlyWhy(0));
    const QString before = describe(*od.uniflex());
    {
        const QString path = work + "/uf-changes.dsk";
        QFile::remove(path);
        QFile::copy(dir + "/uf-basic.dsk", path);
        Flex::OpenedDisc oc;
        err.clear();
        CHECK(openUniflex(path, &oc, &err) && oc.readOnlyWhy(0).isEmpty(), "a copy opens writable: " + err);
        if (oc.uniflex()) uniflexChanges(oc, path);
    }
    // Save As: the same bytes, read back
    const QString copy = work + "/uf-copy.dsk";
    QFile::remove(copy);
    CHECK(od.saveAs(copy, dw::device::Kind::Raw, &err) && slurp(copy) == slurp(dir + "/uf-basic.dsk"), "Save As: byte for byte: " + err);
    Flex::OpenedDisc again;
    CHECK(openUniflex(copy, &again, &err) && describe(*again.uniflex()) == before, "the copy reads the same");

    // uf-many.dsk: a directory of 340 files, its map reaching an indirect block
    Flex::OpenedDisc many;
    err.clear();
    CHECK(openUniflex(dir + "/uf-many.dsk", &many, &err) && namesIn(many.disc().volume(0), Path({"lots"})).size() == 340 &&
          many.disc().volume(0)->read(Path({"lots", "f339"}), &data, &err) && data == crText({"file 339"}) && many.disc().volume(0)->check().ok(),
          "uf-many.dsk: 340 files in /lots, the last one read: " + err);
}

// The damaged discs: each fault named, the disc still listed where it can be
static void uniflexFaults(const QString &dir) {
    printf("UniFLEX: damaged discs\n");
    struct Want { const char *file; const char *error; };
    const Want wants[] = {
        {"uf-bad-dup.dsk", "is in both /readme and /Prog"},
        {"uf-bad-freecount.dsk", "the SIR counts 418 free blocks, but its free list holds 417"},
        {"uf-bad-links.dsk", "/readme: its link count is 2, but 1 directory entry names it"},
        {"uf-bad-freefdn.dsk", "/Prog: its entry names fdn 4, which is free"},
        {"uf-bad-chain.dsk", "the free list names block 16776960, outside the volume"},
        {"uf-bad-map.dsk", "/src/big.dat: a double or triple indirect block names block 649, outside the volume"},
        {"uf-short.dsk", "the image ends at block 1999, but the volume goes on to block 2463"},
        {"uf-noroot.dsk", "/: no . entry"},
    };
    for (const Want &w : wants) {
        Flex::OpenedDisc od;
        QString err;
        const bool open = openUniflex(dir + "/" + w.file, &od, &err);
        const dw::fs::Report r = open ? od.disc().volume(0)->check() : dw::fs::Report();
        bool named = false;
        for (const QString &e : r.errors) if (e.contains(w.error)) named = true;
        CHECK(open && named, QString("%1: %2").arg(w.file, r.errors.join("; ")));
    }
    Flex::OpenedDisc od;
    QString err;
    QByteArray data;
    if (openUniflex(dir + "/uf-bad-map.dsk", &od, &err)) {
        dw::fs::Entry e;
        CHECK(od.disc().volume(0)->stat(Path({"src", "big.dat"}), &e, &err) && !e.problem.isEmpty() &&
              !od.disc().volume(0)->read(Path({"src", "big.dat"}), &data, &err), "a file whose map is broken is listed with its problem and not read: " + err);
        CHECK(od.disc().volume(0)->read(Path({"readme"}), &data, &err), "the others still read");
    }
    Flex::OpenedDisc odd;
    if (openUniflex(dir + "/uf-odd.dsk", &odd, &err)) {
        const dw::fs::Report r = odd.disc().volume(0)->check();
        CHECK(r.ok() && r.warnings.size() == 1 && r.warnings[0].contains("\\x0adynacalc"), "uf-odd.dsk: a warning, the name shown with \\x0a: " + r.warnings.join("; "));
        CHECK(odd.disc().volume(0)->read(Path({"\ndynacalc"}), &data, &err) && data == "odd\r", "and it reads");
    }
}

static void uniflexNames() {
    printf("UniFLEX names\n");
    UniFlex::NameRules n;
    QString why;
    CHECK(n.valid("readme", dw::fs::NodeKind::File, &why) && n.valid("Two Words.c", dw::fs::NodeKind::File, &why) &&
          n.valid("fourteen_chars", dw::fs::NodeKind::File, &why), "valid names");
    CHECK(!n.valid("fifteen_chars.x", dw::fs::NodeKind::File, &why), "14 characters at most: " + why);
    CHECK(!n.valid("a/b", dw::fs::NodeKind::File, &why) && !n.valid("..", dw::fs::NodeKind::Dir, &why), "no '/', not . or ..: " + why);
    CHECK(n.caseSensitivity() == Qt::CaseSensitive, "case-sensitive");
    CHECK(n.fromHost("a very long name.txt", dw::fs::NodeKind::File) == "a very long na", "from a host name: " + n.fromHost("a very long name.txt", dw::fs::NodeKind::File));
}

// Every UniFLEX sample: it opens as UniFLEX, writable exactly when it is healthy; a reload is faithful; every file
// that has no problem reads
static void uniflexSamples(const QString &dir) {
    printf("the UniFLEX sample discs (%s)\n", qPrintable(dir));
    int n = 0;
    for (const QString &f : QDir(dir).entryList({"*.dsk"}, QDir::Files, QDir::Name)) {
        Flex::OpenedDisc od;
        QString err;
        if (!openUniflex(dir + "/" + f, &od, &err)) { CHECK(false, f + ": " + err); continue; }
        ++n;
        const UniFlex::Fs &fs = *od.uniflex();
        const QString before = describe(fs);
        int read = 0, bad = 0;
        for (const UniFlex::Node &node : fs.nodes()) {
            const UniFlex::Fdn *g = fs.fdnOf(node);
            if (&node == &fs.root() || !g || g->isDir() || !node.problem.isEmpty()) continue;
            QByteArray data;
            if (fs.readFile(node, &data, &err) && data.size() == (g->isDevice() ? 0 : g->size)) ++read;
            else ++bad;
        }
        const bool healthy = od.disc().volume(0)->check().ok() && !od.uniflex()->shortImage();
        CHECK(od.readOnlyWhy(0).isEmpty() == healthy && od.uniflex()->reload(&err) && describe(*od.uniflex()) == before && bad == 0,
              QString("%1: %2, a faithful reload, %3 files read").arg(f, healthy ? "writable" : "read-only: " + od.readOnlyWhy(0)).arg(read));
    }
    CHECK(n == 37, QString("%1 UniFLEX samples").arg(n));
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: voltest <dir made by flex_check.py make and uniflex_check.py make> [<sample discs> [<UniFLEX samples>]]\n"); return 2; }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const QString work = QDir::temp().filePath("flexvoltest");
    QDir().mkpath(work);
    detection(dir);
    basic(dir, work);
    changes(dir, work);
    newDiscs(dir, work);
    track0(dir, work);
    containers(dir, work);
    faults(dir);
    names();
    uniflexBasic(dir, work);
    uniflexFaults(dir);
    uniflexNames();
    if (argc > 2) samples(QString::fromLocal8Bit(argv[2]));
    if (argc > 3) uniflexSamples(QString::fromLocal8Bit(argv[3]));
    QDir(work).removeRecursively();
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
