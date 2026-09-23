/* $XFree86: xc/programs/xterm/wcwidth.character,v 1.3 2001/07/29 22:08:16 tsi Exp $ */
/*
 * This is an implementation of wcwidth() and wcswidth() as defined in
 * "The Single UNIX Specification, Version 2, The Open Group, 1997"
 * <http://www.UNIX-systems.org/online.html>
 *
 * Markus Kuhn -- 2001-01-12 -- public domain
 */

#include <QString>

#ifdef HAVE_UTF8PROC
#include <utf8proc.h>
#elif !defined(_WIN32)
#include <cwchar>
#endif

#include "konsole_wcwidth.h"

#if !defined(HAVE_UTF8PROC) && defined(_WIN32)
// NOTE(lvdterm): mingw's C library has no wcwidth() (it's POSIX, not
// part of the Windows CRT). This is the classic public-domain
// implementation this file is named after (Markus Kuhn, 2001-01-12,
// see header above), used here as the portable fallback instead of
// libc's wcwidth(). See VENDORING.md.
namespace {

struct WidthInterval { char32_t first, last; };

bool inTable(char32_t ucs, const WidthInterval* table, int size)
{
    int lo = 0, hi = size - 1;
    if (ucs < table[0].first || ucs > table[hi].last)
        return false;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (ucs > table[mid].last)
            lo = mid + 1;
        else if (ucs < table[mid].first)
            hi = mid - 1;
        else
            return true;
    }
    return false;
}

// Zero-width combining marks and other non-spacing characters.
const WidthInterval kCombining[] = {
    { 0x0300, 0x036F }, { 0x0483, 0x0489 }, { 0x0591, 0x05BD },
    { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 }, { 0x05C4, 0x05C5 },
    { 0x05C7, 0x05C7 }, { 0x0610, 0x061A }, { 0x064B, 0x065F },
    { 0x0670, 0x0670 }, { 0x06D6, 0x06DC }, { 0x06DF, 0x06E4 },
    { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED }, { 0x0711, 0x0711 },
    { 0x0730, 0x074A }, { 0x07A6, 0x07B0 }, { 0x07EB, 0x07F3 },
    { 0x0816, 0x0819 }, { 0x081B, 0x0823 }, { 0x0825, 0x0827 },
    { 0x0829, 0x082D }, { 0x0859, 0x085B }, { 0x08E3, 0x0902 },
    { 0x093A, 0x093A }, { 0x093C, 0x093C }, { 0x0941, 0x0948 },
    { 0x094D, 0x094D }, { 0x0951, 0x0957 }, { 0x0962, 0x0963 },
    { 0x1AB0, 0x1AFF }, { 0x1DC0, 0x1DFF }, { 0x200B, 0x200F },
    { 0x20D0, 0x20FF }, { 0xFE00, 0xFE0F }, { 0xFE20, 0xFE2F },
    { 0xFEFF, 0xFEFF },
};

// East-Asian Wide and Fullwidth ranges (width 2).
const WidthInterval kWide[] = {
    { 0x1100, 0x115F }, { 0x2E80, 0x303E }, { 0x3041, 0x33FF },
    { 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xA000, 0xA4CF },
    { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF }, { 0xFE30, 0xFE4F },
    { 0xFF00, 0xFF60 }, { 0xFFE0, 0xFFE6 }, { 0x1F300, 0x1F64F },
    { 0x1F900, 0x1F9FF }, { 0x20000, 0x3FFFD },
};

} // namespace

static int mk_wcwidth(char32_t ucs)
{
    if (ucs == 0)
        return 0;
    if (ucs < 0x20 || (ucs >= 0x7F && ucs < 0xA0))
        return -1; // control character
    if (inTable(ucs, kCombining, sizeof(kCombining) / sizeof(kCombining[0])))
        return 0;
    if (inTable(ucs, kWide, sizeof(kWide) / sizeof(kWide[0])))
        return 2;
    return 1;
}
#endif

int konsole_wcwidth(wchar_t ucs)
{
#ifdef HAVE_UTF8PROC
    utf8proc_category_t cat = utf8proc_category( ucs );
    if (cat == UTF8PROC_CATEGORY_CO) {
        // Co: Private use area. libutf8proc makes them zero width, while tmux
        // assumes them to be width 1, and glibc's default width is also 1
        return 1;
    }
    return utf8proc_charwidth( ucs );
#elif defined(_WIN32)
    return mk_wcwidth( static_cast<char32_t>(ucs) );
#else
    return wcwidth( ucs );
#endif
}

// single byte char: +1, multi byte char: +2
int string_width( const std::wstring & wstr )
{
    int w = 0;
    for ( size_t i = 0; i < wstr.length(); ++i ) {
        w += konsole_wcwidth( wstr[ i ] );
    }
    return w;
}
