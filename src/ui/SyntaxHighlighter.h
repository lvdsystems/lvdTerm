#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

struct SyntaxConfig;

// Lightweight, dependency-free syntax colouring for the SFTP file editor:
// the common config/script/source formats you meet on a server, coloured in
// the spirit of VS Code's Dark+ / Light+ themes (picked from the current
// palette). It is a small hand-written scanner, not a full parser - good
// enough to tell keywords, strings, comments and numbers apart.
class SyntaxHighlighter : public QSyntaxHighlighter
{
public:
    enum class Language { None, Shell, Python, C, Java, JavaScript, Go, Rust, Json, Yaml, Ini, Xml };

    explicit SyntaxHighlighter(QTextDocument *document);

    // Picks a language from the file name, falling back to a "#!" or
    // "<?xml" first line for files with no telling extension.
    static Language detect(const QString &fileName, const QString &firstLine);
    static QString languageName(Language language);

    Language language() const { return m_language; }
    void setLanguage(Language language);
    void setDark(bool dark);

protected:
    void highlightBlock(const QString &text) override;

private:
    void applyColors();
    int scanCode(const QString &text, int start, int state, const SyntaxConfig &config);
    int highlightXml(const QString &text, int state);
    void highlightIni(const QString &text);
    void highlightYaml(const QString &text, int state);

    Language m_language = Language::None;
    bool m_dark = true;

    QTextCharFormat m_keyword;
    QTextCharFormat m_control;
    QTextCharFormat m_type;
    QTextCharFormat m_function;
    QTextCharFormat m_constant;
    QTextCharFormat m_variable;
    QTextCharFormat m_string;
    QTextCharFormat m_comment;
    QTextCharFormat m_number;
};
