#include "SyntaxHighlighter.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringView>

// Block states carried from one line to the next.
namespace
{
enum State
{
    kNormal = 0,
    kBlockComment = 1,
    kTripleSingle = 2,
    kTripleDouble = 3,
    kBacktick = 4,
    kXmlComment = 5,
    kXmlTag = 6,
};

QSet<QString> words(const char *list)
{
    const QStringList parts = QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return QSet<QString>(parts.begin(), parts.end());
}

bool isIdentChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}
} // namespace

struct SyntaxConfig
{
    QSet<QString> control;  // if / else / return ...
    QSet<QString> keywords; // def / class / const ...
    QSet<QString> types;
    QSet<QString> builtins; // coloured like function calls
    QSet<QString> constants;
    QStringList lineComments;
    QString blockStart;
    QString blockEnd;
    QString quotes; // characters that open a single-line string
    bool tripleQuotes = false;
    bool multilineBacktick = false;
    bool commentNeedsSpace = false; // "#" only starts a comment after whitespace
    bool quoteNeedsBoundary = false; // a quote only opens a string at a word start (prose has apostrophes)
    bool shellVars = false;
    bool decorators = false;
    bool bangMacros = false;
    bool keyedStrings = false; // "key": value
    bool preprocessor = false;
};

namespace
{
const SyntaxConfig &configFor(SyntaxHighlighter::Language language)
{
    using L = SyntaxHighlighter::Language;
    static const QHash<L, SyntaxConfig> configs = [] {
        QHash<L, SyntaxConfig> h;

        SyntaxConfig shell;
        shell.control = words("if then else elif fi for while until do done case esac in select return break continue exit");
        shell.keywords = words("function local export readonly declare typeset unset set shift source alias unalias eval exec trap");
        shell.builtins = words("echo printf cd pwd read test kill wait sudo grep sed awk cat ls rm mv cp mkdir");
        shell.constants = words("true false");
        shell.lineComments = {QStringLiteral("#")};
        shell.quotes = QStringLiteral("\"'`");
        shell.commentNeedsSpace = true;
        shell.shellVars = true;
        h.insert(L::Shell, shell);

        SyntaxConfig py;
        py.control = words("if elif else for while break continue return try except finally raise with assert pass yield await async");
        py.keywords = words("def class import from as global nonlocal del in is not and or lambda");
        py.types = words("int str float bool list dict set tuple bytes object type");
        py.builtins = words("len range print open isinstance super enumerate zip map filter sorted min max sum abs any all input repr format iter next hasattr getattr setattr");
        py.constants = words("True False None self cls");
        py.lineComments = {QStringLiteral("#")};
        py.quotes = QStringLiteral("\"'");
        py.tripleQuotes = true;
        py.decorators = true;
        h.insert(L::Python, py);

        SyntaxConfig c;
        c.control = words("if else for while do switch case default break continue return goto try catch throw co_await co_return co_yield");
        c.keywords = words("sizeof typedef struct union enum class namespace template typename using public private protected virtual override final static const "
                           "constexpr consteval inline extern volatile new delete this operator explicit friend mutable register noexcept decltype auto "
                           "static_assert thread_local alignas asm concept requires export import module");
        c.types = words("int long short char float double void bool unsigned signed size_t ssize_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t "
                        "int64_t wchar_t char8_t char16_t char32_t");
        c.constants = words("true false NULL nullptr");
        c.lineComments = {QStringLiteral("//")};
        c.blockStart = QStringLiteral("/*");
        c.blockEnd = QStringLiteral("*/");
        c.quotes = QStringLiteral("\"'");
        c.preprocessor = true;
        h.insert(L::C, c);

        SyntaxConfig java;
        java.control = words("if else for while do switch case default break continue return throw try catch finally yield");
        java.keywords = words("abstract assert class const enum extends final implements import instanceof interface native new package private protected public "
                              "static strictfp super synchronized this throws transient volatile var record sealed permits void namespace using internal "
                              "readonly override virtual async await partial");
        java.types = words("int long short byte char float double boolean String Object Integer Long List Map bool decimal uint ulong object");
        java.constants = words("true false null");
        java.lineComments = {QStringLiteral("//")};
        java.blockStart = QStringLiteral("/*");
        java.blockEnd = QStringLiteral("*/");
        java.quotes = QStringLiteral("\"'");
        java.decorators = true;
        h.insert(L::Java, java);

        SyntaxConfig js;
        js.control = words("if else for while do switch case default break continue return throw try catch finally yield await");
        js.keywords = words("var let const function class extends new delete typeof instanceof in of void this super import export async static interface enum "
                            "implements public private protected readonly declare namespace abstract with debugger");
        js.types = words("string number boolean any unknown never object Array Map Set Promise Date Object String Number Symbol RegExp Error JSON Math");
        js.constants = words("true false null undefined NaN Infinity");
        js.lineComments = {QStringLiteral("//")};
        js.blockStart = QStringLiteral("/*");
        js.blockEnd = QStringLiteral("*/");
        js.quotes = QStringLiteral("\"'");
        js.multilineBacktick = true;
        js.decorators = true;
        h.insert(L::JavaScript, js);

        SyntaxConfig go;
        go.control = words("if else for switch case default break continue return goto select go defer fallthrough range");
        go.keywords = words("func package import var const type struct interface map chan");
        go.types = words("int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 uintptr float32 float64 complex64 complex128 string bool byte rune error any");
        go.constants = words("true false nil iota");
        go.lineComments = {QStringLiteral("//")};
        go.blockStart = QStringLiteral("/*");
        go.blockEnd = QStringLiteral("*/");
        go.quotes = QStringLiteral("\"'");
        go.multilineBacktick = true;
        h.insert(L::Go, go);

        SyntaxConfig rust;
        rust.control = words("if else for while loop match break continue return");
        rust.keywords = words("fn let mut const static struct enum impl trait pub use mod crate self Self super as where move ref dyn async await unsafe extern type in box");
        rust.types = words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option Result Box");
        rust.constants = words("true false None Some Ok Err");
        rust.lineComments = {QStringLiteral("//")};
        rust.blockStart = QStringLiteral("/*");
        rust.blockEnd = QStringLiteral("*/");
        rust.quotes = QStringLiteral("\""); // ' is also a lifetime marker, so chars stay plain
        rust.bangMacros = true;
        h.insert(L::Rust, rust);

        SyntaxConfig json;
        json.constants = words("true false null");
        json.lineComments = {QStringLiteral("//")}; // .jsonc
        json.blockStart = QStringLiteral("/*");
        json.blockEnd = QStringLiteral("*/");
        json.quotes = QStringLiteral("\"");
        json.keyedStrings = true;
        h.insert(L::Json, json);

        SyntaxConfig yaml;
        yaml.constants = words("true false null yes no on off True False Yes No ~");
        yaml.lineComments = {QStringLiteral("#")};
        yaml.quotes = QStringLiteral("\"'");
        yaml.commentNeedsSpace = true;
        yaml.quoteNeedsBoundary = true;
        h.insert(L::Yaml, yaml);

        SyntaxConfig ini; // values only: whole-line comments are handled by highlightIni
        ini.constants = words("true false yes no on off null");
        ini.quotes = QStringLiteral("\"'");
        ini.quoteNeedsBoundary = true;
        h.insert(L::Ini, ini);

        return h;
    }();
    static const SyntaxConfig empty;
    const auto it = configs.constFind(language);
    return it == configs.constEnd() ? empty : *it;
}
} // namespace

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document) : QSyntaxHighlighter(document)
{
    applyColors();
}

QString SyntaxHighlighter::languageName(Language language)
{
    switch (language) {
    case Language::Shell: return QStringLiteral("Shell");
    case Language::Python: return QStringLiteral("Python");
    case Language::C: return QStringLiteral("C/C++");
    case Language::Java: return QStringLiteral("Java/C#");
    case Language::JavaScript: return QStringLiteral("JavaScript/TypeScript");
    case Language::Go: return QStringLiteral("Go");
    case Language::Rust: return QStringLiteral("Rust");
    case Language::Json: return QStringLiteral("JSON");
    case Language::Yaml: return QStringLiteral("YAML");
    case Language::Ini: return QStringLiteral("Config");
    case Language::Xml: return QStringLiteral("XML/HTML");
    case Language::None: break;
    }
    return QStringLiteral("Plain text");
}

SyntaxHighlighter::Language SyntaxHighlighter::detect(const QString &fileName, const QString &firstLine)
{
    using L = Language;
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    const QString lowerName = fileName.toLower();

    static const QHash<QString, L> bySuffix = {
        {"sh", L::Shell}, {"bash", L::Shell}, {"zsh", L::Shell}, {"ksh", L::Shell}, {"bashrc", L::Shell}, {"zshrc", L::Shell},
        {"profile", L::Shell}, {"bash_profile", L::Shell}, {"bash_aliases", L::Shell}, {"mk", L::Shell},
        {"py", L::Python}, {"pyw", L::Python},
        {"c", L::C}, {"h", L::C}, {"cc", L::C}, {"cpp", L::C}, {"cxx", L::C}, {"hpp", L::C}, {"hh", L::C}, {"hxx", L::C}, {"ino", L::C},
        {"java", L::Java}, {"cs", L::Java}, {"kt", L::Java},
        {"js", L::JavaScript}, {"mjs", L::JavaScript}, {"cjs", L::JavaScript}, {"jsx", L::JavaScript}, {"ts", L::JavaScript}, {"tsx", L::JavaScript},
        {"go", L::Go}, {"rs", L::Rust},
        {"json", L::Json}, {"jsonc", L::Json}, {"json5", L::Json},
        {"yaml", L::Yaml}, {"yml", L::Yaml},
        {"ini", L::Ini}, {"cfg", L::Ini}, {"conf", L::Ini}, {"toml", L::Ini}, {"properties", L::Ini}, {"service", L::Ini}, {"socket", L::Ini},
        {"timer", L::Ini}, {"desktop", L::Ini}, {"env", L::Ini}, {"rules", L::Ini},
        {"xml", L::Xml}, {"html", L::Xml}, {"htm", L::Xml}, {"xhtml", L::Xml}, {"svg", L::Xml}, {"xsl", L::Xml}, {"plist", L::Xml}, {"ui", L::Xml},
        {"qrc", L::Xml},
    };
    const auto it = bySuffix.constFind(suffix);
    if (it != bySuffix.constEnd())
        return *it;

    if (lowerName == QLatin1String("makefile") || lowerName == QLatin1String("dockerfile") || lowerName == QLatin1String("gnumakefile"))
        return L::Shell;
    if (lowerName.endsWith(QLatin1String("_config")) || lowerName == QLatin1String("config") || lowerName == QLatin1String("fstab"))
        return L::Ini;

    if (firstLine.startsWith(QLatin1String("#!"))) {
        if (firstLine.contains(QLatin1String("python")))
            return L::Python;
        if (firstLine.contains(QLatin1String("node")))
            return L::JavaScript;
        static const QRegularExpression shellRe(QStringLiteral("\\b(ba|z|k|da|a)?sh\\b"));
        if (shellRe.match(firstLine).hasMatch())
            return L::Shell;
    }
    if (firstLine.startsWith(QLatin1String("<?xml")) || firstLine.startsWith(QLatin1String("<!DOCTYPE"), Qt::CaseInsensitive))
        return L::Xml;
    return L::None;
}

void SyntaxHighlighter::setLanguage(Language language)
{
    if (m_language == language)
        return;
    m_language = language;
    rehighlight();
}

void SyntaxHighlighter::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    applyColors();
    rehighlight();
}

void SyntaxHighlighter::applyColors()
{
    // VS Code's Dark+ / Light+ token colours.
    const auto fmt = [](const char *color) {
        QTextCharFormat f;
        f.setForeground(QColor(QString::fromLatin1(color)));
        return f;
    };
    if (m_dark) {
        m_keyword = fmt("#569CD6");
        m_control = fmt("#C586C0");
        m_type = fmt("#4EC9B0");
        m_function = fmt("#DCDCAA");
        m_constant = fmt("#4FC1FF");
        m_variable = fmt("#9CDCFE");
        m_string = fmt("#CE9178");
        m_comment = fmt("#6A9955");
        m_number = fmt("#B5CEA8");
    } else {
        m_keyword = fmt("#0000FF");
        m_control = fmt("#AF00DB");
        m_type = fmt("#267F99");
        m_function = fmt("#795E26");
        m_constant = fmt("#0070C1");
        m_variable = fmt("#001080");
        m_string = fmt("#A31515");
        m_comment = fmt("#008000");
        m_number = fmt("#098658");
    }
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    const int previous = previousBlockState() < 0 ? kNormal : previousBlockState();
    setCurrentBlockState(kNormal);

    switch (m_language) {
    case Language::None:
        return;
    case Language::Xml:
        setCurrentBlockState(highlightXml(text, previous));
        return;
    case Language::Ini:
        highlightIni(text);
        return;
    case Language::Yaml:
        highlightYaml(text, previous);
        return;
    default: {
        const SyntaxConfig &config = configFor(m_language);
        if (config.preprocessor && previous == kNormal && text.trimmed().startsWith(QLatin1Char('#'))) {
            setFormat(0, text.size(), m_control);
            return;
        }
        setCurrentBlockState(scanCode(text, 0, previous, config));
        return;
    }
    }
}

// The generic scanner: strings, comments, numbers, words. Runs over text
// from `start`, entering in `state` (inside a block comment or multi-line
// string from an earlier line); returns the state to carry to the next line.
int SyntaxHighlighter::scanCode(const QString &text, int start, int state, const SyntaxConfig &config)
{
    const int n = text.size();
    int i = start;
    QString previousWord;

    const auto startsWith = [&](int at, const QString &needle) { return !needle.isEmpty() && QStringView(text).sliced(at).startsWith(needle); };

    while (i < n) {
        if (state != kNormal) {
            QString end;
            switch (state) {
            case kBlockComment: end = config.blockEnd; break;
            case kTripleSingle: end = QStringLiteral("'''"); break;
            case kTripleDouble: end = QStringLiteral("\"\"\""); break;
            default: end = QStringLiteral("`"); break;
            }
            const QTextCharFormat &format = state == kBlockComment ? m_comment : m_string;
            const int e = text.indexOf(end, i);
            if (e < 0) {
                setFormat(i, n - i, format);
                return state;
            }
            setFormat(i, e + end.size() - i, format);
            i = e + end.size();
            state = kNormal;
            continue;
        }

        const QChar ch = text.at(i);

        bool lineComment = false;
        for (const QString &prefix : config.lineComments) {
            if (!startsWith(i, prefix))
                continue;
            if (config.commentNeedsSpace && i > 0 && !text.at(i - 1).isSpace())
                continue;
            lineComment = true;
            break;
        }
        if (lineComment) {
            setFormat(i, n - i, m_comment);
            return kNormal;
        }

        if (startsWith(i, config.blockStart)) {
            setFormat(i, config.blockStart.size(), m_comment);
            i += config.blockStart.size();
            state = kBlockComment;
            continue;
        }
        if (config.tripleQuotes && (startsWith(i, QStringLiteral("\"\"\"")) || startsWith(i, QStringLiteral("'''")))) {
            setFormat(i, 3, m_string);
            state = ch == QLatin1Char('"') ? kTripleDouble : kTripleSingle;
            i += 3;
            continue;
        }
        if (config.multilineBacktick && ch == QLatin1Char('`')) {
            setFormat(i, 1, m_string);
            state = kBacktick;
            ++i;
            continue;
        }

        if (config.quotes.contains(ch) && (!config.quoteNeedsBoundary || i == 0 || text.at(i - 1).isSpace() || QStringLiteral("[{,:(").contains(text.at(i - 1)))) {
            int j = i + 1;
            while (j < n && text.at(j) != ch) {
                if (text.at(j) == QLatin1Char('\\'))
                    ++j;
                ++j;
            }
            const int stop = qMin(j + 1, n); // include the closing quote; an unterminated one runs to the end of the line
            bool key = false;
            if (config.keyedStrings && j < n) {
                int k = stop;
                while (k < n && text.at(k).isSpace())
                    ++k;
                key = k < n && text.at(k) == QLatin1Char(':');
            }
            setFormat(i, stop - i, key ? m_variable : m_string);
            i = stop;
            previousWord.clear();
            continue;
        }

        if (ch.isDigit() && (i == 0 || !isIdentChar(text.at(i - 1)))) {
            int j = i + 1;
            while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('.') || text.at(j) == QLatin1Char('_')))
                ++j;
            setFormat(i, j - i, m_number);
            i = j;
            previousWord.clear();
            continue;
        }

        if (ch.isLetter() || ch == QLatin1Char('_') || (ch == QLatin1Char('$') && !config.shellVars)) {
            int j = i + 1;
            while (j < n && (isIdentChar(text.at(j)) || (text.at(j) == QLatin1Char('$') && !config.shellVars)))
                ++j;
            const QString word = text.mid(i, j - i);

            int next = j;
            while (next < n && text.at(next).isSpace())
                ++next;

            const QTextCharFormat *format = nullptr;
            if (config.control.contains(word))
                format = &m_control;
            else if (config.keywords.contains(word))
                format = &m_keyword;
            else if (config.types.contains(word))
                format = &m_type;
            else if (config.builtins.contains(word))
                format = &m_function;
            else if (config.constants.contains(word))
                format = &m_constant;
            else if (previousWord == QLatin1String("class") || previousWord == QLatin1String("struct") || previousWord == QLatin1String("enum")
                     || previousWord == QLatin1String("interface") || previousWord == QLatin1String("namespace") || previousWord == QLatin1String("trait"))
                format = &m_type;
            else if (next < n && text.at(next) == QLatin1Char('('))
                format = &m_function;
            else if (config.bangMacros && j < n && text.at(j) == QLatin1Char('!') && !(j + 1 < n && text.at(j + 1) == QLatin1Char('=')))
                format = &m_function;

            if (format)
                setFormat(i, j - i, *format);
            previousWord = word;
            i = j;
            continue;
        }

        if (config.decorators && ch == QLatin1Char('@') && i + 1 < n && text.at(i + 1).isLetter()) {
            int j = i + 1;
            while (j < n && (isIdentChar(text.at(j)) || text.at(j) == QLatin1Char('.')))
                ++j;
            setFormat(i, j - i, m_function);
            i = j;
            previousWord.clear();
            continue;
        }

        if (config.shellVars && ch == QLatin1Char('$') && i + 1 < n) {
            int j = i + 1;
            const QChar next = text.at(j);
            if (next == QLatin1Char('{')) {
                const int close = text.indexOf(QLatin1Char('}'), j);
                j = close < 0 ? n : close + 1;
            } else if (next.isLetter() || next == QLatin1Char('_')) {
                while (j < n && isIdentChar(text.at(j)))
                    ++j;
            } else if (QStringLiteral("@#?*!$-0123456789").contains(next)) {
                ++j;
            }
            if (j > i + 1) {
                setFormat(i, j - i, m_variable);
                i = j;
                previousWord.clear();
                continue;
            }
        }

        if (!ch.isSpace())
            previousWord.clear();
        ++i;
    }
    return state;
}

int SyntaxHighlighter::highlightXml(const QString &text, int state)
{
    const int n = text.size();
    int i = 0;
    while (i < n) {
        if (state == kXmlComment) {
            const int e = text.indexOf(QStringLiteral("-->"), i);
            if (e < 0) {
                setFormat(i, n - i, m_comment);
                return kXmlComment;
            }
            setFormat(i, e + 3 - i, m_comment);
            i = e + 3;
            state = kNormal;
            continue;
        }

        if (state == kNormal) {
            const int lt = text.indexOf(QLatin1Char('<'), i);
            if (lt < 0)
                return kNormal;
            if (QStringView(text).sliced(lt).startsWith(QLatin1String("<!--"))) {
                setFormat(lt, 4, m_comment);
                i = lt + 4;
                state = kXmlComment;
                continue;
            }
            int k = lt + 1;
            if (k < n && (text.at(k) == QLatin1Char('/') || text.at(k) == QLatin1Char('?') || text.at(k) == QLatin1Char('!')))
                ++k;
            while (k < n && (text.at(k).isLetterOrNumber() || QStringLiteral(":-_.").contains(text.at(k))))
                ++k;
            setFormat(lt, k - lt, m_keyword);
            i = k;
            state = kXmlTag;
            continue;
        }

        // Inside a tag: attributes, values and the closing bracket.
        const QChar ch = text.at(i);
        if ((ch == QLatin1Char('/') || ch == QLatin1Char('?')) && i + 1 < n && text.at(i + 1) == QLatin1Char('>')) {
            setFormat(i, 2, m_keyword);
            i += 2;
            state = kNormal;
        } else if (ch == QLatin1Char('>')) {
            setFormat(i, 1, m_keyword);
            ++i;
            state = kNormal;
        } else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            const int close = text.indexOf(ch, i + 1);
            const int stop = close < 0 ? n : close + 1;
            setFormat(i, stop - i, m_string);
            i = stop;
        } else if (ch.isLetter() || ch == QLatin1Char('_')) {
            int j = i + 1;
            while (j < n && (text.at(j).isLetterOrNumber() || QStringLiteral(":-_.").contains(text.at(j))))
                ++j;
            setFormat(i, j - i, m_variable);
            i = j;
        } else {
            ++i;
        }
    }
    return state;
}

void SyntaxHighlighter::highlightIni(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1Char(';'))) {
        setFormat(0, text.size(), m_comment);
        return;
    }
    static const QRegularExpression sectionRe(QStringLiteral("^\\s*\\[[^\\]]*\\]"));
    const QRegularExpressionMatch section = sectionRe.match(text);
    if (section.hasMatch()) {
        setFormat(0, section.capturedLength(), m_type);
        return;
    }
    int start = 0;
    static const QRegularExpression keyRe(QStringLiteral("^\\s*([A-Za-z0-9_][\\w.\\-/@]*)\\s*(?:=|:|\\s|$)"));
    const QRegularExpressionMatch key = keyRe.match(text);
    if (key.hasMatch()) {
        setFormat(key.capturedStart(1), key.capturedLength(1), m_variable);
        start = key.capturedEnd(1);
    }
    scanCode(text, start, kNormal, configFor(Language::Ini));
}

void SyntaxHighlighter::highlightYaml(const QString &text, int state)
{
    const SyntaxConfig &config = configFor(Language::Yaml);
    int start = 0;
    if (state == kNormal) {
        static const QRegularExpression keyRe(QStringLiteral("^(\\s*(?:-\\s+)*)(\"[^\"]*\"|'[^']*'|[^\\s#'\"\\-:][^#:]*?)(\\s*:)(?=\\s|$)"));
        const QRegularExpressionMatch key = keyRe.match(text);
        if (key.hasMatch()) {
            setFormat(key.capturedStart(2), key.capturedLength(2), m_variable);
            start = key.capturedEnd(3);
        }
    }
    setCurrentBlockState(scanCode(text, start, state, config));
}
