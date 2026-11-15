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

    printf("changes are refused, and leave nothing behind\n");
    const QString before = describe(*od.disk());
    dw::fs::Disc &disc = od.disc();
    const auto refused = [&](const char *what, const std::function<bool(QString *)> &f) {
        QString e2;
        const bool ok = disc.transact(f, &e2);
        CHECK(!ok && e2 == Flex::FlexVolume::ReadOnlyText && !od.dirty(), QString(what) + " refused: " + e2);
    };
    refused("write", [&](QString *e2) { return v->write(Path(), "NEW.TXT", "x", {}, QDateTime(), false, e2); });
    refused("remove", [&](QString *e2) { return v->remove(Path({"README.TXT"}), e2); });
    refused("rename", [&](QString *e2) { return v->rename(Path({"README.TXT"}), Path({"OTHER.TXT"}), e2); });
    refused("setAttrs", [&](QString *e2) { return v->setAttrs(Path({"README.TXT"}), {{"write", true}}, e2); });
    refused("setVolumeAttrs", [&](QString *e2) { return v->setVolumeAttrs({{"label", "X"}}, e2); });
    CHECK(od.readOnlyWhy(0) == Flex::FlexVolume::ReadOnlyText, "read-only: " + od.readOnlyWhy(0));
    CHECK(v->reload(&err) && describe(*od.disk()) == before, "a reload gives the same state");

    printf("Save As: a copy, byte for byte, read back\n");
    const QString copy = work + "/basic-copy.dsk";
    QFile::remove(copy);
    CHECK(od.saveAs(copy, dw::device::Kind::Raw, &err), "saved as a raw copy: " + err);
    CHECK(slurp(copy) == slurp(dir + "/basic.dsk"), "identical bytes");
    CHECK(!od.saveAs(work + "/basic.imd", dw::device::Kind::Imd, &err), "ImageDisk refused: " + err);
    Flex::OpenedDisc od2;
    CHECK(openDisc(copy, &od2, &err) && describe(*od2.disk()) == before, "the copy reads the same");
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
    Flex::OpenedDisc od;
    QString err;
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
    const QStringList damaged = {"C009.DSK", "FLX503TX.DSK"};
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
        if (damaged.contains(f)) CHECK(!r.ok(), f + ": " + r.errors.value(0));
        else CHECK(r.ok() && od.readOnlyWhy(0) == Flex::FlexVolume::ReadOnlyText, f + ": no errors");
        const QString before = describe(*od.disk());
        CHECK(od.disk()->reload(&err) && describe(*od.disk()) == before, f + ": a reload gives the same state");
    }
    CHECK(flex == 15, QString("%1 FLEX discs").arg(flex));
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: voltest <dir made by flex_check.py make> [<sample discs>]\n"); return 2; }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const QString work = QDir::temp().filePath("flexvoltest");
    QDir().mkpath(work);
    detection(dir);
    basic(dir, work);
    faults(dir);
    names();
    if (argc > 2) samples(QString::fromLocal8Bit(argv[2]));
    QDir(work).removeRecursively();
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
