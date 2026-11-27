#include "image-clipboard.h"

static void bmp_saved(GObject *stream, GAsyncResult *result, gpointer data)
{
    GdkContentSerializer *serializer = data;
    GError *error = NULL;
    if (gdk_pixbuf_save_to_stream_finish(result, &error)) {
        gdk_content_serializer_return_success(serializer);
    } else {
        gdk_content_serializer_return_error(serializer, error);
    }
    g_object_unref(serializer);
    (void)stream;
}

static void serialize_bmp(GdkContentSerializer *serializer)
{
    GdkTexture *texture = g_value_get_object(gdk_content_serializer_get_value(serializer));
    /* Retain compatibility with GTK 4.10. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_get_from_texture(texture);
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (!pixels) {
        gdk_content_serializer_return_error(serializer, g_error_new_literal(
            G_IO_ERROR, G_IO_ERROR_FAILED, "Could not read the image pixels."));
        return;
    }

    gdk_pixbuf_save_to_stream_async(pixels, gdk_content_serializer_get_output_stream(serializer),
                                    "bmp", gdk_content_serializer_get_cancellable(serializer),
                                    bmp_saved, g_object_ref(serializer), NULL);
}

void bridge_clipboard_offer_image(GdkClipboard *clipboard, GdkTexture *image)
{
    static gsize registered;
    if (g_once_init_enter(&registered)) {
        gdk_content_register_serializer(GDK_TYPE_TEXTURE, "image/bmp", serialize_bmp, NULL, NULL);
        g_once_init_leave(&registered, 1);
    }
    gdk_clipboard_set_texture(clipboard, image);
}
