#include "JpegDecoder.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

bool decodeJpegToRgb(const uchar *data, int size, int *width, int *height, QByteArray *rgb)
{
    int w = 0;
    int h = 0;
    int components = 0;
    stbi_uc *pixels = stbi_load_from_memory(data, size, &w, &h, &components, 3);
    if (!pixels)
        return false;
    *width = w;
    *height = h;
    *rgb = QByteArray(reinterpret_cast<const char *>(pixels), w * h * 3);
    stbi_image_free(pixels);
    return true;
}
