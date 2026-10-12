#include "CodeEditor.h"

#include <QPainter>
#include <QTextBlock>

#include "SyntaxHighlighter.h"

namespace
{
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }

private:
    CodeEditor *m_editor;
};
} // namespace

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this))
{
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    m_highlighter = new SyntaxHighlighter(document());
    m_highlighter->setDark(palette().color(QPalette::Base).lightness() < 128);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

QString CodeEditor::setSyntax(const QString &fileName, const QString &firstLine)
{
    const SyntaxHighlighter::Language language = SyntaxHighlighter::detect(fileName, firstLine);
    m_highlighter->setLanguage(language);
    return SyntaxHighlighter::languageName(language);
}

int CodeEditor::setSearchTerm(const QString &term, bool caseSensitive)
{
    m_searchSelections.clear();
    if (!term.isEmpty()) {
        QTextDocument::FindFlags flags;
        if (caseSensitive)
            flags |= QTextDocument::FindCaseSensitively;

        QTextEdit::ExtraSelection match;
        match.format.setBackground(QColor(234, 92, 0, 85));
        QTextCursor cursor(document());
        while (m_searchSelections.size() < kMaxHighlightedMatches) {
            cursor = document()->find(term, cursor, flags);
            if (cursor.isNull())
                break;
            match.cursor = cursor;
            m_searchSelections.append(match);
        }
    }
    highlightCurrentLine();
    return m_searchSelections.size();
}

bool CodeEditor::findText(const QString &term, bool caseSensitive, bool backwards, bool incremental)
{
    if (term.isEmpty())
        return false;

    QTextDocument::FindFlags flags;
    if (caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;
    if (backwards)
        flags |= QTextDocument::FindBackward;

    QTextCursor from = textCursor();
    if (incremental)
        from.setPosition(from.selectionStart());
    QTextCursor found = document()->find(term, from, flags);
    if (found.isNull()) {
        QTextCursor wrap(document());
        if (backwards)
            wrap.movePosition(QTextCursor::End);
        found = document()->find(term, wrap, flags);
    }
    if (found.isNull())
        return false;
    setTextCursor(found);
    return true;
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
        ++digits;
    // Room for at least 3 digits so the gutter doesn't jump around as a
    // small file grows.
    digits = qMax(digits, 3);
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    // Font or theme changed: the gutter width and the highlight colour
    // both depend on them.
    if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange) {
        if (m_highlighter)
            m_highlighter->setDark(palette().color(QPalette::Base).lightness() < 128);
        updateLineNumberAreaWidth();
        highlightCurrentLine();
        m_lineNumberArea->update();
    }
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly() && isEnabled()) {
        // A translucent wash of the selection colour reads as a subtle band
        // on both light and dark backgrounds.
        QColor band = palette().color(QPalette::Highlight);
        band.setAlpha(45);

        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(band);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    selections.append(m_searchSelections);
    setExtraSelections(selections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    const QPalette &pal = palette();
    painter.fillRect(event->rect(), pal.color(QPalette::Window));

    // Hairline between the gutter and the text.
    painter.setPen(pal.color(QPalette::Mid));
    painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(), m_lineNumberArea->width() - 1, event->rect().bottom());

    const int currentBlock = textCursor().blockNumber();
    const QColor dim = pal.color(QPalette::Disabled, QPalette::Text);
    const QColor bright = pal.color(QPalette::Text);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    QFont normal = font();
    QFont bold = normal;
    bold.setBold(true);

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const bool current = blockNumber == currentBlock;
            painter.setFont(current ? bold : normal);
            painter.setPen(current ? bright : dim);
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
