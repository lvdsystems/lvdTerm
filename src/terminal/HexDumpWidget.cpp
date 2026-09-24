#include "HexDumpWidget.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

namespace
{
// Every row's hex column is padded to this width (computed once from a
// full-width dummy row) so a short trailing row still lines its ASCII
// column up with every row above it, rather than hand-deriving the
// group-boundary arithmetic - simpler and less error-prone.
QString hexPartOnly(const QByteArray &rowBytes, HexDumpWidget::Grouping grouping)
{
    QString hexPart;
    const int groupSize = static_cast<int>(grouping);
    for (int i = 0; i < rowBytes.size(); ++i) {
        hexPart += QStringLiteral("%1").arg(static_cast<unsigned char>(rowBytes.at(i)), 2, 16, QLatin1Char('0')).toUpper();
        const bool isLastByte = (i == rowBytes.size() - 1);
        // A single space *only* at a group boundary - bytes within the
        // same group sit flush against each other (e.g. Word: "0102 0304",
        // not "01 02 03 04" or "0102  0304"). Byte grouping's group size
        // is 1, so every byte is its own group boundary - the familiar
        // fully-spaced-out look ("00 01 02 03...") falls out of this
        // same rule rather than needing its own special case.
        if (!isLastByte && (i + 1) % groupSize == 0)
            hexPart += QLatin1Char(' ');
    }
    return hexPart;
}

int fullRowHexWidth(HexDumpWidget::Grouping grouping)
{
    return hexPartOnly(QByteArray(16, '\0'), grouping).length();
}
} // namespace

HexDumpWidget::HexDumpWidget(QWidget *parent) : QWidget(parent), m_textEdit(new QPlainTextEdit(this)), m_groupingCombo(new QComboBox(this))
{
    m_textEdit->setReadOnly(true);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEdit->setUndoRedoEnabled(false); // never edited by the user - no undo history to keep
    QFont monoFont(QStringLiteral("Consolas"));
    monoFont.setStyleHint(QFont::Monospace);
    m_textEdit->setFont(monoFont);

    m_groupingCombo->addItem(QStringLiteral("Byte"), static_cast<int>(Grouping::Byte));
    m_groupingCombo->addItem(QStringLiteral("Word"), static_cast<int>(Grouping::Word));
    m_groupingCombo->addItem(QStringLiteral("Dword"), static_cast<int>(Grouping::Dword));
    connect(m_groupingCombo, &QComboBox::currentIndexChanged, this, [this] { setGrouping(static_cast<Grouping>(m_groupingCombo->currentData().toInt())); });

    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 2, 6, 2);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("Group by:"), toolbar));
    toolbarLayout->addWidget(m_groupingCombo);
    toolbarLayout->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(m_textEdit, 1);
}

void HexDumpWidget::appendData(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    // Incremental, O(new bytes) append - NOT a full reformat of
    // everything received so far. A first version did exactly that
    // (reformat the whole buffer on every appendData() call) and a
    // standalone test sending a sustained ~1.2 MB stream in 16-byte
    // chunks hung for minutes: reformatting a growing up-to-1-MiB buffer
    // on *every* chunk is the same O(n²) shape as the drag-select
    // clipboard bug found earlier in this project - exactly the class of
    // bug this rewrite exists to avoid repeating.
    m_buffer += data;

    QStringList newLines;
    while (m_buffer.size() - m_formattedOffset >= kBytesPerRow) {
        const QByteArray row = m_buffer.mid(static_cast<int>(m_formattedOffset), kBytesPerRow);
        newLines << formatRow(m_droppedByteCount + m_formattedOffset, row, m_grouping);
        m_formattedOffset += kBytesPerRow;
    }
    if (!newLines.isEmpty()) {
        const bool wasAtBottom = m_textEdit->verticalScrollBar()->value() >= m_textEdit->verticalScrollBar()->maximum() - 4;
        m_textEdit->appendPlainText(newLines.join(QLatin1Char('\n')));
        if (wasAtBottom)
            m_textEdit->moveCursor(QTextCursor::End);
    }

    // Trim complete, *already-formatted* rows from the front once over
    // the cap - the still-forming tail (fewer than kBytesPerRow bytes,
    // never yet formatted) is never touched, so it always finishes the
    // row it's already partway through. Rare relative to appendData()
    // itself (only once the cap is actually exceeded), so the O(rows
    // trimmed) cost of removing the corresponding old blocks from the
    // text edit below is fine.
    if (m_buffer.size() > kMaxBufferedBytes) {
        const qint64 excess = m_buffer.size() - kMaxBufferedBytes;
        const qint64 rowsToTrim = (excess + kBytesPerRow - 1) / kBytesPerRow;
        const qint64 bytesToTrim = qMin(rowsToTrim * kBytesPerRow, m_formattedOffset);
        if (bytesToTrim > 0) {
            m_buffer.remove(0, static_cast<int>(bytesToTrim));
            m_droppedByteCount += bytesToTrim;
            m_formattedOffset -= bytesToTrim;

            QTextCursor cursor(m_textEdit->document());
            cursor.movePosition(QTextCursor::Start);
            for (qint64 i = 0; i < bytesToTrim / kBytesPerRow; ++i) {
                cursor.select(QTextCursor::BlockUnderCursor);
                cursor.removeSelectedText();
                if (!cursor.atEnd())
                    cursor.deleteChar(); // the newline left behind after removing the block's own text
            }
        }
    }
}

void HexDumpWidget::clear()
{
    m_buffer.clear();
    m_droppedByteCount = 0;
    m_formattedOffset = 0;
    m_textEdit->clear();
}

void HexDumpWidget::setGrouping(Grouping grouping)
{
    if (m_grouping == grouping)
        return;
    m_grouping = grouping;
    if (m_groupingCombo->currentData().toInt() != static_cast<int>(grouping))
        m_groupingCombo->setCurrentIndex(m_groupingCombo->findData(static_cast<int>(grouping)));
    rebuildDisplay(); // the one case that DOES need a full reformat: grouping reflows every row already shown, not just future ones
}

void HexDumpWidget::rebuildDisplay()
{
    QStringList lines;
    for (qint64 offset = 0; offset < m_formattedOffset; offset += kBytesPerRow) {
        const QByteArray row = m_buffer.mid(static_cast<int>(offset), kBytesPerRow);
        lines << formatRow(m_droppedByteCount + offset, row, m_grouping);
    }

    const bool wasAtBottom = m_textEdit->verticalScrollBar()->value() >= m_textEdit->verticalScrollBar()->maximum() - 4;
    m_textEdit->setPlainText(lines.join(QLatin1Char('\n')));
    if (wasAtBottom)
        m_textEdit->moveCursor(QTextCursor::End);
}

QString HexDumpWidget::formatRow(qint64 offset, const QByteArray &rowBytes, Grouping grouping)
{
    QString ascii;
    for (int i = 0; i < rowBytes.size(); ++i) {
        const unsigned char b = static_cast<unsigned char>(rowBytes.at(i));
        ascii += (b >= 0x20 && b < 0x7f) ? QChar(QLatin1Char(static_cast<char>(b))) : QChar(QLatin1Char('.'));
    }

    const QString hexPart = hexPartOnly(rowBytes, grouping).leftJustified(fullRowHexWidth(grouping));
    return QStringLiteral("%1  %2  |%3|").arg(QStringLiteral("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper(), hexPart, ascii);
}
