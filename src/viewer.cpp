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
    int accepts(const QByteArray &d, const QVariantMap &) const override { return Flex::looksLikeText(d) ? 2 : 0; }
    dw::ViewResult render(const QByteArray &d, const QVariantMap &) const override {
        dw::ViewResult r;
        r.text = Flex::expandText(d);
        r.note = "FLEX text: CR line ends, runs of spaces expanded";
        return r;
    }
};

const dw::ViewerRegistry &flexViewers() {
    static dw::ViewerRegistry reg = [] {
        dw::ViewerRegistry r;
        dw::addDefaultViewers(r);
        r.remove("plain");                      // FLEX's own Text replaces the core's
        r.add(std::make_shared<FlexTextViewer>());
        return r;
    }();
    return reg;
}
} // namespace

ViewerDialog::ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent)
    : dw::ViewerDialog(flexViewers(), title, data, parent) {}
