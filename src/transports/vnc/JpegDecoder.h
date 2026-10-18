#pragma once

#include <QByteArray>

// Decodes a JPEG (what the Tight encoding sends for photographic areas) with
// the vendored stb_image (third_party/stb, public domain / MIT) - a plain
// decoder with no dependency on Qt image plugins, which a static Qt build
// would otherwise have to link in.
//
// On success `rgb` holds width*height*3 bytes (R, G, B per pixel).
bool decodeJpegToRgb(const uchar *data, int size, int *width, int *height, QByteArray *rgb);
