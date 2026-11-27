#include "image-clipboard.h"

int main(void)
{
    gtk_init();
    static const guint8 rgba[] = { 230, 40, 60, 255, 22, 170, 70, 255 };
    g_autoptr(GBytes) bytes = g_bytes_new_static(rgba, sizeof rgba);
    g_autoptr(GdkTexture) image = gdk_memory_texture_new(2, 1, GDK_MEMORY_R8G8B8A8, bytes, 8);
    bridge_clipboard_offer_image(gdk_display_get_clipboard(gdk_display_get_default()), image);
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_print("ready\n");
    g_main_loop_run(loop);
    return 0;
}
