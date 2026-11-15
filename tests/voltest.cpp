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
// FLEX volume as a dw::fs::Disc (listing, reading, attributes, the check), every change refused in a transaction
// that leaves nothing staged (series flex-1 only reads), a faithful reload, Save As, and the sample discs.
//   voltest <dir made by flex_check.py make> [<the sample discs' folder>]
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>

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

// Everything the Disk holds in memory, as text: what reload() must give back exactly.
static QString describe(const Flex::Disk &d) {
    QStringList out;
    for (const Flex::File &f : d.files())
        out << QString("%1 %2 %3 a%4 %5-%6 n%7 r%8 %9/%10/%11 [%12]").arg(f.slot).arg(f.name, QString::fromLatin1(f.rawName.toHex()))
                   .arg(f.attr).arg(f.start.toString(), f.end.toString()).arg(f.sectors).arg(f.random).arg(f.month).arg(f.day).arg(f.year).arg(f.problem);
    const Flex::Sir &s = d.sir();
    out << QString("sir %1 %2 %3-%4 %5 %6/%7").arg(QString::fromLatin1(s.label.toHex())).arg(s.volumeNumber).arg(s.firstFree.toString(), s.lastFree.toString())
               .arg(s.freeCount).arg(s.lastTrack).arg(s.sectors);
    out << QString("dir %1 deleted %2 free %3").arg(d.dirSectors().size()).arg(d.deletedEntries()).arg(d.freeSlots());
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
        {"os9.dsk", "os9", dw::fs::Tier::Likely, false, dw::fs::Verdict::Open},
        {"uniflex.dsk", "uniflex", dw::fs::Tier::Possible, false, dw::fs::Verdict::Open},
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
        CHECK(Flex::createBlank(path, p, "NEW", 9, QDate(2026, 11, 15), QByteArray(), &err), p.id + " made: " + err);
        const dw::fs::Verdict vd = verdictOf(path);
        Flex::OpenedDisc od;
        CHECK(QFileInfo(path).size() == qint64(p.bytes) && vd.bestCandidate() && vd.bestCandidate()->tier == dw::fs::Tier::Certain &&
              openDisc(path, &od, &err) && od.readOnlyWhy(0).isEmpty(), p.id + ": FLEX, certain, writable");
        if (!od.disk()) continue;
        const Flex::Sir &s = od.disk()->sir();
        CHECK(s.labelText() == "NEW" && s.volumeNumber == 9 && s.date() == QDate(2026, 11, 15) && s.freeCount == (tracks - 1) * sectors &&
              s.firstFree == Flex::TS(1, 1) && s.lastFree == Flex::TS(tracks - 1, sectors) && od.disk()->dirSectors().size() == sectors - 4 &&
              od.disk()->files().isEmpty() && !od.disk()->hasLoader(), p.id + ": SIR, free chain, empty directory, no loader");
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
    const QStringList foreign = {"C012_1.DSK", "C014.DSK", "C014.DSK.CORRUPTED", "C022.DSK", "C022_1.DSK"};
    int flex = 0;
    for (const QString &f : QDir(dir).entryList(QDir::Files, QDir::Name)) {
        const QString p = dir + "/" + f;
        const dw::fs::Verdict v = verdictOf(p);
        const dw::fs::Candidate *c = v.bestCandidate();
        if (foreign.contains(f)) {
            CHECK(c && !c->supported && (c->fsId == "os9" || c->fsId == "uniflex"), f + ": " + (c ? c->description : QString("nothing")));
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

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: voltest <dir made by flex_check.py make> [<sample discs>]\n"); return 2; }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const QString work = QDir::temp().filePath("flexvoltest");
    QDir().mkpath(work);
    detection(dir);
    basic(dir, work);
    changes(dir, work);
    newDiscs(dir, work);
    faults(dir);
    names();
    if (argc > 2) samples(QString::fromLocal8Bit(argv[2]));
    QDir(work).removeRecursively();
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
