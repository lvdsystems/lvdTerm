#include "KeyboardProfiles.h"

#include <QBuffer>

#include "KeyboardTranslator.h"

namespace
{
// Sourced verbatim from upstream QTermWidget's lib/kb-layouts/ (not
// vendored as loose files - see KeyboardProfiles.h and VENDORING.md's
// established rationale for keeping everything compiled in). Backslash
// escapes ("\E", "\t", "\x7f", ...) are the keytab format's own escape
// syntax, parsed by KeyboardTranslatorReader - not C++ escapes - which is
// exactly why these are raw string literals: the text below is byte-for-
// byte what the original .keytab files contain.

// lib/kb-layouts/default.keytab - the *real* default keymap (arrow keys,
// Home/End, Insert/Delete, F1-F12, application-keypad/cursor-key mode,
// scrollback shortcuts). Registered under the name "default", which is
// what KeyboardTranslatorManager::defaultTranslator() looks for first.
const char *const kDefaultKeytab = R"KEYTAB(
keyboard "Default (XFree 4)"

key Escape             : "\E"

key Tab   -Shift       : "\t"
key Tab   +Shift+Ansi  : "\E[Z"
key Tab   +Shift-Ansi  : "\t"
key Backtab     +Ansi  : "\E[Z"
key Backtab     -Ansi  : "\t"

key Return-Shift-NewLine : "\r"
key Return-Shift+NewLine : "\r\n"

key Return+Shift         : "\EOM"

key Backspace -Control : "\b"
key Backspace +Control : "\x7f"

key Up   -Shift-Ansi : "\EA"
key Down -Shift-Ansi : "\EB"
key Right-Shift-Ansi : "\EC"
key Left -Shift-Ansi : "\ED"

key Up    -Shift-AnyMod+Ansi+AppCuKeys : "\EOA"
key Down  -Shift-AnyMod+Ansi+AppCuKeys : "\EOB"
key Right -Shift-AnyMod+Ansi+AppCuKeys : "\EOC"
key Left  -Shift-AnyMod+Ansi+AppCuKeys : "\EOD"

key Up    -Shift-AnyMod+Ansi-AppCuKeys : "\E[A"
key Down  -Shift-AnyMod+Ansi-AppCuKeys : "\E[B"
key Right -Shift-AnyMod+Ansi-AppCuKeys : "\E[C"
key Left  -Shift-AnyMod+Ansi-AppCuKeys : "\E[D"

key Up    -Shift+AnyMod+Ansi           : "\E[1;*A"
key Down  -Shift+AnyMod+Ansi           : "\E[1;*B"
key Right -Shift+AnyMod+Ansi           : "\E[1;*C"
key Left  -Shift+AnyMod+Ansi           : "\E[1;*D"

key Up    +Shift+AppScreen             : "\E[1;*A"
key Down  +Shift+AppScreen             : "\E[1;*B"
key Left  +Shift+AppScreen             : "\E[1;*D"
key Right +Shift+AppScreen             : "\E[1;*C"
key PgUp    +Shift+AppScreen           : "\E[5;2~"
key PgDown  +Shift+AppScreen           : "\E[6;2~"

key Up    -Shift+Ansi+AppCuKeys+KeyPad : "\EOA"
key Down  -Shift+Ansi+AppCuKeys+KeyPad : "\EOB"
key Right -Shift+Ansi+AppCuKeys+KeyPad : "\EOC"
key Left  -Shift+Ansi+AppCuKeys+KeyPad : "\EOD"

key Up    -Shift+Ansi-AppCuKeys+KeyPad : "\E[A"
key Down  -Shift+Ansi-AppCuKeys+KeyPad : "\E[B"
key Right -Shift+Ansi-AppCuKeys+KeyPad : "\E[C"
key Left  -Shift+Ansi-AppCuKeys+KeyPad : "\E[D"

key Home        +AppCuKeys+KeyPad : "\EOH"
key End         +AppCuKeys+KeyPad : "\EOF"
key Home        -AppCuKeys+KeyPad : "\E[H"
key End         -AppCuKeys+KeyPad : "\E[F"

key Insert        +KeyPad : "\E[2~"
key Delete        +KeyPad : "\E[3~"
key PgUp    -Shift+KeyPad : "\E[5~"
key PgDown  -Shift+KeyPad : "\E[6~"

key Clear -AnyMod+KeyPad+AppKeyPad : "\E[OE"
key Clear +AnyMod+KeyPad+AppKeyPad : "\E[1;*E"

key Enter+NewLine : "\r\n"
key Enter-NewLine : "\r"

key Home        -AnyMod-AppCuKeys : "\E[H"
key End         -AnyMod-AppCuKeys : "\E[F"
key Home        -AnyMod+AppCuKeys : "\EOH"
key End         -AnyMod+AppCuKeys : "\EOF"
key Home        +AnyMod           : "\E[1;*H"
key End         +AnyMod           : "\E[1;*F"

key Insert      -AnyMod  : "\E[2~"
key Delete      -AnyMod  : "\E[3~"
key Insert      +AnyMod  : "\E[2;*~"
key Delete      +AnyMod  : "\E[3;*~"

key PgUp    -Shift-AnyMod : "\E[5~"
key PgDown  -Shift-AnyMod : "\E[6~"
key PgUp    -Shift+AnyMod : "\E[5;*~"
key PgDown  -Shift+AnyMod : "\E[6;*~"

key F1  -AnyMod  : "\EOP"
key F2  -AnyMod  : "\EOQ"
key F3  -AnyMod  : "\EOR"
key F4  -AnyMod  : "\EOS"
key F5  -AnyMod  : "\E[15~"
key F6  -AnyMod  : "\E[17~"
key F7  -AnyMod  : "\E[18~"
key F8  -AnyMod  : "\E[19~"
key F9  -AnyMod  : "\E[20~"
key F10 -AnyMod  : "\E[21~"
key F11 -AnyMod  : "\E[23~"
key F12 -AnyMod  : "\E[24~"

key F1  +AnyMod  : "\EO*P"
key F2  +AnyMod  : "\EO*Q"
key F3  +AnyMod  : "\EO*R"
key F4  +AnyMod  : "\EO*S"
key F5  +AnyMod  : "\E[15;*~"
key F6  +AnyMod  : "\E[17;*~"
key F7  +AnyMod  : "\E[18;*~"
key F8  +AnyMod  : "\E[19;*~"
key F9  +AnyMod  : "\E[20;*~"
key F10 +AnyMod  : "\E[21;*~"
key F11 +AnyMod  : "\E[23;*~"
key F12 +AnyMod  : "\E[24;*~"

key Space +Control : "\x00"

key Up      +Shift-AppScreen : scrollLineUp
key PgUp    +Shift-AppScreen : scrollPageUp
key Home    +Shift-AppScreen : scrollUpToTop
key Down    +Shift-AppScreen : scrollLineDown
key PgDown  +Shift-AppScreen : scrollPageDown
key End     +Shift-AppScreen : scrollDownToBottom

key ScrollLock     : scrollLock
)KEYTAB";

// lib/kb-layouts/linux.keytab - Linux console conventions (notably
// Backspace sends DEL/0x7f unconditionally, and F1-F5 use the Linux
// console's own escape sequences rather than xterm's).
const char *const kLinuxKeytab = R"KEYTAB(
keyboard "Linux console"

key Escape : "\E"
key Tab    : "\t"

key Return-NewLine : "\r"
key Return+NewLine : "\r\n"

key Backspace : "\x7f"
key Delete    : "\E[3~"

key Up   -Shift-Ansi : "\EA"
key Down -Shift-Ansi : "\EB"
key Right-Shift-Ansi : "\EC"
key Left -Shift-Ansi : "\ED"

key Up   -Shift+Ansi+AppCuKeys : "\EOA"
key Down -Shift+Ansi+AppCuKeys : "\EOB"
key Right-Shift+Ansi+AppCuKeys : "\EOC"
key Left -Shift+Ansi+AppCuKeys : "\EOD"

key Up   -Shift+Ansi-AppCuKeys : "\E[A"
key Down -Shift+Ansi-AppCuKeys : "\E[B"
key Right-Shift+Ansi-AppCuKeys : "\E[C"
key Left -Shift+Ansi-AppCuKeys : "\E[D"

key Up    -Shift+AnyMod+Ansi           : "\E[1;*A"
key Down  -Shift+AnyMod+Ansi           : "\E[1;*B"
key Right -Shift+AnyMod+Ansi           : "\E[1;*C"
key Left  -Shift+AnyMod+Ansi           : "\E[1;*D"

key F1 : "\E[[A"
key F2 : "\E[[B"
key F3 : "\E[[C"
key F4 : "\E[[D"
key F5 : "\E[[E"

key F6     : "\E[17~"
key F7     : "\E[18~"
key F8     : "\E[19~"
key F9     : "\E[20~"
key F10    : "\E[21~"
key F11    : "\E[23~"
key F12    : "\E[24~"

key Home   : "\E[1~"
key End    : "\E[4~"

key PgUp    -Shift : "\E[5~"
key PgDown  -Shift : "\E[6~"
key Insert  -Shift : "\E[2~"

key Enter+NewLine : "\r\n"
key Enter-NewLine : "\r"

key Space +Control : "\x00"

key Up      +Shift : scrollLineUp
key PgUp    +Shift : scrollPageUp
key Down    +Shift : scrollLineDown
key PgDown  +Shift : scrollPageDown

key ScrollLock     : scrollLock
)KEYTAB";

// lib/kb-layouts/historic/vt100.keytab - a real VT100 terminal's own
// (rather idiosyncratic) function-key/Home/End sequences.
const char *const kVt100Keytab = R"KEYTAB(
keyboard "vt100 (historical)"

key Escape : "\E"
key Tab    : "\t"

key Return-NewLine : "\r"
key Return+NewLine : "\r\n"

key Backspace : "\x7f"
key Delete    : "\E[3~"

key Up   -Shift-Ansi : "\EA"
key Down -Shift-Ansi : "\EB"
key Right-Shift-Ansi : "\EC"
key Left -Shift-Ansi : "\ED"

key Up   -Shift+Ansi+AppCuKeys : "\EOA"
key Down -Shift+Ansi+AppCuKeys : "\EOB"
key Right-Shift+Ansi+AppCuKeys : "\EOC"
key Left -Shift+Ansi+AppCuKeys : "\EOD"

key Up   -Shift+Ansi-AppCuKeys : "\E[A"
key Down -Shift+Ansi-AppCuKeys : "\E[B"
key Right-Shift+Ansi-AppCuKeys : "\E[C"
key Left -Shift+Ansi-AppCuKeys : "\E[D"

key F1     : "\E[11~"
key F2     : "\E[12~"
key F3     : "\E[13~"
key F4     : "\E[14~"
key F5     : "\E[15~"

key F6     : "\E[17~"
key F7     : "\E[18~"
key F8     : "\E[19~"
key F9     : "\E[20~"
key F10    : "\E[21~"
key F11    : "\E[23~"
key F12    : "\E[24~"

key Home   : "\E[H"
key End    : "\E[F"

key PgUp    -Shift : "\E[5~"
key PgDown  -Shift : "\E[6~"
key Insert  -Shift : "\E[2~"

key Enter+NewLine : "\r\n"
key Enter-NewLine : "\r"

key Space +Control : "\x00"

key Up     +Shift : scrollLineUp
key PgUp   +Shift : scrollPageUp
key Down   +Shift : scrollLineDown
key PgDown +Shift : scrollPageDown

key ScrollLock     : scrollLock
)KEYTAB";

void registerOne(const QString &name, const char *keytabText)
{
    QByteArray bytes(keytabText);
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);

    auto *translator = new Konsole::KeyboardTranslator(name);
    Konsole::KeyboardTranslatorReader reader(&buffer);
    translator->setDescription(reader.description());
    while (reader.hasNextEntry())
        translator->addEntry(reader.nextEntry());

    // KeyboardTranslatorManager::addTranslator()'s own on-disk save path
    // is a stubbed-out no-op upstream (saveTranslator() is #if 0'd out),
    // so this only ever registers the translator in memory.
    Konsole::KeyboardTranslatorManager::instance()->addTranslator(translator);
}
} // namespace

namespace KeyboardProfiles
{

void registerAll()
{
    registerOne(QStringLiteral("default"), kDefaultKeytab);
    registerOne(QStringLiteral("linux"), kLinuxKeytab);
    registerOne(QStringLiteral("vt100"), kVt100Keytab);
}

QStringList names()
{
    return {QStringLiteral("default"), QStringLiteral("linux"), QStringLiteral("vt100")};
}

} // namespace KeyboardProfiles
