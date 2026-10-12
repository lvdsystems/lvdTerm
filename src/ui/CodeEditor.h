#pragma once

#include <QPlainTextEdit>
#include <QTextEdit>

class SyntaxHighlighter;

// QPlainTextEdit with a line-number gutter on the left and a highlight on
// the line holding the cursor, syntax colouring chosen from the file name,
// and find support (highlight every match, step through them). Colours come
// from the application palette, so it follows the Light/Dark theme.
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    // Colours the text for the file being edited; call before loading it.
    // `firstLine` is used for "#!" detection. Returns the language name.
    QString setSyntax(const QString &fileName, const QString &firstLine);

    // Highlights every occurrence of `term` (empty clears); returns how
    // many were found, capped at kMaxHighlightedMatches.
    static constexpr int kMaxHighlightedMatches = 5000;
    int setSearchTerm(const QString &term, bool caseSensitive);
    // Selects the next/previous occurrence, wrapping around. `incremental`
    // searches from the start of the current selection, so typing more of
    // the term keeps the current match instead of jumping on.
    bool findText(const QString &term, bool caseSensitive, bool backwards, bool incremental = false);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

    QWidget *m_lineNumberArea = nullptr;
    SyntaxHighlighter *m_highlighter = nullptr;
    QList<QTextEdit::ExtraSelection> m_searchSelections;
};
