#include "wine-clipboard.h"

GBytes *bridge_wine_image_payload(GdkTexture *image, GError **error)
{
    guint width = gdk_texture_get_width(image);
    guint height = gdk_texture_get_height(image);
    if (width > 16384 || height > 16384 || width * (guint64)height > 32u * 1024u * 1024u) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "The clipboard image is too large.");
        return NULL;
    }

    /* GTK 4.10 does not have GdkTextureDownloader. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_get_from_texture(image);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_autofree char *bmp = NULL;
    gsize bmp_size = 0;
    if (!pixels || !gdk_pixbuf_save_to_buffer(pixels, &bmp, &bmp_size, "bmp", error, NULL)) {
        if (!pixels)
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "The image pixels could not be read.");
        return NULL;
    }
    g_autoptr(GBytes) png = gdk_texture_save_to_png_bytes(image);
    gsize png_size = 0;
    const guint8 *png_data = png ? g_bytes_get_data(png, &png_size) : NULL;
    if (bmp_size < 54 || !png_data || bmp_size - 14 > (128u << 20) || png_size > (128u << 20)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "The image could not be encoded for Wine.");
        return NULL;
    }

    const guint32 sizes[] = { GUINT32_TO_LE((guint32)(bmp_size - 14)), GUINT32_TO_LE((guint32)png_size) };
    GByteArray *payload = g_byte_array_sized_new(sizeof(sizes) + bmp_size - 14 + png_size);
    g_byte_array_append(payload, (const guint8 *)sizes, sizeof(sizes));
    g_byte_array_append(payload, (const guint8 *)bmp + 14, bmp_size - 14);
    g_byte_array_append(payload, png_data, png_size);
    return g_byte_array_free_to_bytes(payload);
}
