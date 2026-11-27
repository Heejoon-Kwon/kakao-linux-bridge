#include "image-clipboard.h"
#include "wine-clipboard.h"
#include <string.h>

int main(int argc, char **argv)
{
    gtk_init();
    static const guint8 rgba[] = { 230, 40, 60, 255, 22, 170, 70, 255 };
    g_autoptr(GBytes) bytes = g_bytes_new_static(rgba, sizeof rgba);
    g_autoptr(GdkTexture) image = gdk_memory_texture_new(2, 1, GDK_MEMORY_R8G8B8A8, bytes, 8);
    if (argc == 2 && g_str_equal(argv[1], "--verify-frame")) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GBytes) payload = bridge_wine_image_payload(image, &error);
        g_assert_no_error(error);
        g_assert_nonnull(payload);
        gsize length;
        const guint8 *data = g_bytes_get_data(payload, &length);
        guint32 sizes[2];
        g_assert_cmpuint(length, >, sizeof(sizes) + 40);
        memcpy(sizes, data, sizeof(sizes));
        guint32 bitmap = GUINT32_FROM_LE(sizes[0]);
        guint32 png = GUINT32_FROM_LE(sizes[1]);
        g_assert_cmpuint(length, ==, sizeof(sizes) + bitmap + png);
        g_assert_cmpuint(bitmap, >=, 40);
        g_assert_cmpuint(png, >=, 8);
        g_assert_cmpuint(data[sizeof(sizes)], ==, 40);
        g_assert_cmpmem(data + sizeof(sizes) + bitmap, 8, "\211PNG\r\n\032\n", 8);
        return 0;
    }
    bridge_clipboard_offer_image(gdk_display_get_clipboard(gdk_display_get_default()), image);
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_print("ready\n");
    g_main_loop_run(loop);
    return 0;
}
