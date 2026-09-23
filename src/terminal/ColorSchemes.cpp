#include "ColorSchemes.h"

using Konsole::ColorEntry;

namespace
{
// clang-format off
const ColorEntry kDark[TABLE_COLORS] = {
    ColorEntry(QColor(0xE5, 0xE5, 0xE5), false), ColorEntry(QColor(0x1E, 0x1E, 0x1E), false), // Dfore, Dback
    ColorEntry(QColor(0x00, 0x00, 0x00), false), ColorEntry(QColor(0xCD, 0x00, 0x00), false), // Black, Red
    ColorEntry(QColor(0x00, 0xCD, 0x00), false), ColorEntry(QColor(0xCD, 0xCD, 0x00), false), // Green, Yellow
    ColorEntry(QColor(0x00, 0x00, 0xEE), false), ColorEntry(QColor(0xCD, 0x00, 0xCD), false), // Blue, Magenta
    ColorEntry(QColor(0x00, 0xCD, 0xCD), false), ColorEntry(QColor(0xE5, 0xE5, 0xE5), false), // Cyan, White
    ColorEntry(QColor(0xFF, 0xFF, 0xFF), false), ColorEntry(QColor(0x1E, 0x1E, 0x1E), false), // Dfore*, Dback*
    ColorEntry(QColor(0x7F, 0x7F, 0x7F), false), ColorEntry(QColor(0xFF, 0x00, 0x00), false), // Black*, Red*
    ColorEntry(QColor(0x00, 0xFF, 0x00), false), ColorEntry(QColor(0xFF, 0xFF, 0x00), false), // Green*, Yellow*
    ColorEntry(QColor(0x5C, 0x5C, 0xFF), false), ColorEntry(QColor(0xFF, 0x00, 0xFF), false), // Blue*, Magenta*
    ColorEntry(QColor(0x00, 0xFF, 0xFF), false), ColorEntry(QColor(0xFF, 0xFF, 0xFF), false), // Cyan*, White*
};

const ColorEntry kLight[TABLE_COLORS] = {
    ColorEntry(QColor(0x00, 0x00, 0x00), false), ColorEntry(QColor(0xFF, 0xFF, 0xFF), false), // Dfore, Dback
    ColorEntry(QColor(0x00, 0x00, 0x00), false), ColorEntry(QColor(0xCD, 0x00, 0x00), false),
    ColorEntry(QColor(0x00, 0x8B, 0x00), false), ColorEntry(QColor(0xB8, 0x86, 0x00), false),
    ColorEntry(QColor(0x00, 0x00, 0xCD), false), ColorEntry(QColor(0xCD, 0x00, 0xCD), false),
    ColorEntry(QColor(0x00, 0x8B, 0x8B), false), ColorEntry(QColor(0x4D, 0x4D, 0x4D), false),
    ColorEntry(QColor(0x00, 0x00, 0x00), false), ColorEntry(QColor(0xFF, 0xFF, 0xFF), false),
    ColorEntry(QColor(0x7F, 0x7F, 0x7F), false), ColorEntry(QColor(0xFF, 0x00, 0x00), false),
    ColorEntry(QColor(0x00, 0xCD, 0x00), false), ColorEntry(QColor(0xCD, 0xCD, 0x00), false),
    ColorEntry(QColor(0x00, 0x00, 0xFF), false), ColorEntry(QColor(0xFF, 0x00, 0xFF), false),
    ColorEntry(QColor(0x00, 0xCD, 0xCD), false), ColorEntry(QColor(0x1A, 0x1A, 0x1A), false),
};

// Standard Solarized Dark (https://ethanschoonover.com/solarized/) ANSI
// mapping.
const ColorEntry kSolarizedDark[TABLE_COLORS] = {
    ColorEntry(QColor(0x83, 0x94, 0x96), false), ColorEntry(QColor(0x00, 0x2B, 0x36), false), // base0, base03
    ColorEntry(QColor(0x07, 0x36, 0x42), false), ColorEntry(QColor(0xDC, 0x32, 0x2F), false), // base02, red
    ColorEntry(QColor(0x85, 0x99, 0x00), false), ColorEntry(QColor(0xB5, 0x89, 0x00), false), // green, yellow
    ColorEntry(QColor(0x26, 0x8B, 0xD2), false), ColorEntry(QColor(0xD3, 0x36, 0x82), false), // blue, magenta
    ColorEntry(QColor(0x2A, 0xA1, 0x98), false), ColorEntry(QColor(0xEE, 0xE8, 0xD5), false), // cyan, base2
    ColorEntry(QColor(0x93, 0xA1, 0xA1), false), ColorEntry(QColor(0x00, 0x2B, 0x36), false), // base1, base03
    ColorEntry(QColor(0x00, 0x2B, 0x36), false), ColorEntry(QColor(0xCB, 0x4B, 0x16), false), // base03, orange
    ColorEntry(QColor(0x58, 0x6E, 0x75), false), ColorEntry(QColor(0x65, 0x7B, 0x83), false), // base01, base00
    ColorEntry(QColor(0x83, 0x94, 0x96), false), ColorEntry(QColor(0x6C, 0x71, 0xC4), false), // base0, violet
    ColorEntry(QColor(0x93, 0xA1, 0xA1), false), ColorEntry(QColor(0xFD, 0xF6, 0xE3), false), // base1, base3
};
// clang-format on

struct Scheme
{
    const char *name;
    const ColorEntry *table;
};

const Scheme kSchemes[] = {
    {"Dark", kDark},
    {"Light", kLight},
    {"Solarized Dark", kSolarizedDark},
};
} // namespace

QStringList ColorSchemes::names()
{
    QStringList result;
    for (const Scheme &s : kSchemes)
        result << QString::fromLatin1(s.name);
    return result;
}

const Konsole::ColorEntry *ColorSchemes::table(const QString &name)
{
    for (const Scheme &s : kSchemes) {
        if (name == QString::fromLatin1(s.name))
            return s.table;
    }
    return kSchemes[0].table;
}
