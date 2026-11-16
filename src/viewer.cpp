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
#include "viewer.h"

#include "flextext.h"

#include <diskwrangler/text/viewers.h>

namespace {
class FlexTextViewer : public dw::FileViewer {
public:
    QString id() const override { return "flextext"; }
    QString name() const override { return "Text"; }
    int order() const override { return 0; }
    int accepts(const QByteArray &d, const QVariantMap &ctx) const override {
        return ctx.value("fs").toString() != "uniflex" && Flex::looksLikeText(d) ? 2 : 0;
    }
    dw::ViewResult render(const QByteArray &d, const QVariantMap &) const override {
        dw::ViewResult r;
        r.text = Flex::expandText(d);
        r.note = "FLEX text: CR line ends, runs of spaces expanded";
        return r;
    }
};

// UniFLEX text: lines end in CR (as on the sample discs; LF and CR LF read too), TABs are TABs.
class UniFlexTextViewer : public dw::FileViewer {
public:
    QString id() const override { return "uniflextext"; }
    QString name() const override { return "Text"; }
    int order() const override { return 0; }
    int accepts(const QByteArray &d, const QVariantMap &ctx) const override {
        if (ctx.value("fs").toString() != "uniflex") return 0;
        int text = 0, other = 0, ends = 0;
        for (char ch : d) {
            const uchar c = uchar(ch);
            if (c == 0x0D || c == 0x0A) { ++ends; ++text; }
            else if ((c >= 0x20 && c < 0x7F) || c == 0x09 || c == 0x0C) ++text;
            else if (c) ++other;
        }
        return text > 0 && ends > 0 && other * 50 <= text ? 2 : 0;
    }
    dw::ViewResult render(const QByteArray &d, const QVariantMap &) const override {
        dw::ViewResult r;
        QString s;
        s.reserve(d.size());
        for (int i = 0; i < d.size(); ++i) {
            const uchar c = uchar(d[i]);
            if (c == 0x0D) { s += '\n'; if (i + 1 < d.size() && d[i + 1] == '\n') ++i; continue; }
            if (c == 0x00) continue;
            s += QChar(c);
        }
        r.text = s;
        r.note = "UniFLEX text: CR line ends";
        return r;
    }
};

// One registry per filesystem, each with its own Text in place of the core's plain one: the View As menu lists every
// view a registry has, so FLEX's and UniFLEX's text views must never be in the same one.
const dw::ViewerRegistry &viewersFor(const QString &fs) {
    auto make = [](std::shared_ptr<dw::FileViewer> text) {
        dw::ViewerRegistry r;
        dw::addDefaultViewers(r);
        r.remove("plain");
        r.add(text);
        return r;
    };
    static const dw::ViewerRegistry flex = make(std::make_shared<FlexTextViewer>());
    static const dw::ViewerRegistry uniflex = make(std::make_shared<UniFlexTextViewer>());
    return fs == "uniflex" ? uniflex : flex;
}
} // namespace

ViewerDialog::ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent, const QString &fs)
    : dw::ViewerDialog(viewersFor(fs), title, data, parent, QVariantMap{{"fs", fs}}) {}
