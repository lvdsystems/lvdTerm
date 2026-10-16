#pragma once

// Where on a pane a dragged pane was dropped (see TerminalView): one of the
// four edges splits the target pane on that side, the middle swaps the two.
enum class PaneDropZone
{
    Left,
    Right,
    Top,
    Bottom,
    Swap,
};
