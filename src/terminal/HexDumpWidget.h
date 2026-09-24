#pragma once

#include <QByteArray>
#include <QWidget>

class QPlainTextEdit;
class QComboBox;

// A read-only, live-growing hex dump of raw bytes - the "viewer: hex"
// alternative to the normal VT100-emulated TerminalDisplay (see
// TerminalView, which owns one of these instead of a TerminalDisplay
// when ConnectionProfile::Viewer::Hex is selected). Bytes are shown
// exactly as received, never interpreted as terminal escape sequences -
// the whole point of this mode is a connection carrying a binary
// protocol rather than an actual shell.
//
// Grouping controls how many raw bytes are visually clustered together
// in the hex column (a space every 1/2/4 bytes) - purely a display
// convenience for the eye, like any hex editor's "byte/word/dword"
// toggle. It does not reorder or reinterpret bytes (no endianness
// applied to word/dword groups); it's still the exact received byte
// order, just clustered differently.
class HexDumpWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Grouping
    {
        Byte = 1,
        Word = 2,
        Dword = 4,
    };

    explicit HexDumpWidget(QWidget *parent = nullptr);

    void appendData(const QByteArray &data);
    void clear();

    void setGrouping(Grouping grouping);
    Grouping grouping() const { return m_grouping; }

private:
    void rebuildDisplay();
    static QString formatRow(qint64 offset, const QByteArray &rowBytes, Grouping grouping);

    QPlainTextEdit *m_textEdit = nullptr;
    QComboBox *m_groupingCombo = nullptr;

    // Every byte received, capped - same bounded-memory reasoning as the
    // terminal's own scrollback (Konsole::HistoryTypeBuffer(5000) lines,
    // see TerminalSession) - a long-running binary session shouldn't grow
    // this widget's memory use without bound. Kept (not just the
    // formatted text) so changing the grouping can reformat everything
    // already received, not just future bytes.
    QByteArray m_buffer;
    qint64 m_droppedByteCount = 0; // bytes trimmed from the front over this session - keeps displayed offsets correct after trimming
    qint64 m_formattedOffset = 0; // index into m_buffer up to which complete rows have already been formatted/appended
    Grouping m_grouping = Grouping::Byte;

    static constexpr qint64 kMaxBufferedBytes = 1 * 1024 * 1024; // 1 MiB
    static constexpr int kBytesPerRow = 16;
};
