// Written from scratch for lvdterm — not part of upstream QTermWidget.
//
// Upstream generates this header via CMake's generate_export_header() for
// a shared-library build of qtermwidget6.dll. lvdterm links this vendored
// core as a static library into a single executable, so there is no DLL
// boundary to annotate; every symbol is visible within that one binary.
// See VENDORING.md.
#pragma once

#define QTERMWIDGET_EXPORT
#define QTERMWIDGET_NO_EXPORT
#define QTERMWIDGET_DEPRECATED
#define QTERMWIDGET_DEPRECATED_EXPORT
#define QTERMWIDGET_DEPRECATED_NO_EXPORT
