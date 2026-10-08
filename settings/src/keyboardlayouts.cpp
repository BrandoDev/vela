// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "keyboardlayouts.h"

#include <QCollator>
#include <QFile>
#include <QVariantMap>
#include <QXmlStreamReader>

#include <algorithm>

void KeyboardLayouts::load()
{
    QFile file(QStringLiteral("/usr/share/X11/xkb/rules/evdev.xml"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    // <layout><configItem><name>it</name><description>Italian</description>
    // </configItem><variantList><variant><configItem>...</configItem></variant>...
    // The descriptions are in English: libxkbcommon has no translations,
    // xkeyboard-config takes them from its gettext domain. They're left as
    // they are here, as KDE does when a translation is missing.
    QXmlStreamReader xml(&file);
    QString layout;
    QString layoutName;
    bool inVariant = false;
    QString name;
    QString description;
    QStringList path;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            path << xml.name().toString();
            if (xml.name() == QLatin1String("variant")) {
                inVariant = true;
                name.clear();
                description.clear();
            } else if (xml.name() == QLatin1String("layout")) {
                layout.clear();
                layoutName.clear();
            } else if (xml.name() == QLatin1String("name") && path.size() >= 2 && path[path.size() - 2] == QLatin1String("configItem")) {
                name = xml.readElementText();
                path.removeLast();
            } else if (xml.name() == QLatin1String("description") && path.size() >= 2
                && path[path.size() - 2] == QLatin1String("configItem")) {
                description = xml.readElementText();
                path.removeLast();
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("configItem") && path.contains(QLatin1String("layoutList"))) {
                if (inVariant) {
                    m_all.append(QVariantMap { { QStringLiteral("layout"), layout }, { QStringLiteral("variant"), name },
                        { QStringLiteral("name"), description } });
                } else if (!path.contains(QLatin1String("variantList"))) {
                    layout = name;
                    layoutName = description;
                    m_all.append(QVariantMap { { QStringLiteral("layout"), layout }, { QStringLiteral("variant"), QString() },
                        { QStringLiteral("name"), description } });
                }
            } else if (xml.name() == QLatin1String("variant")) {
                inVariant = false;
            }
            if (!path.isEmpty()) {
                path.removeLast();
            }
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(m_all.begin(), m_all.end(), [&](const QVariant& a, const QVariant& b) {
        return collator.compare(a.toMap()[QStringLiteral("name")].toString(), b.toMap()[QStringLiteral("name")].toString()) < 0;
    });
}

QVariantList KeyboardLayouts::all()
{
    if (m_all.isEmpty()) {
        load();
    }
    return m_all;
}

QString KeyboardLayouts::name(const QString& layout, const QString& variant)
{
    for (const QVariant& value : all()) {
        const QVariantMap entry = value.toMap();
        if (entry[QStringLiteral("layout")] == layout && entry[QStringLiteral("variant")].toString() == variant) {
            return entry[QStringLiteral("name")].toString();
        }
    }
    return variant.isEmpty() ? layout : layout + QStringLiteral(" (") + variant + u')';
}
