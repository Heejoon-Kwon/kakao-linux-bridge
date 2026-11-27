#pragma once
#include <gtk/gtk.h>

GBytes *bridge_wine_image_payload(GdkTexture *image, GError **error);
