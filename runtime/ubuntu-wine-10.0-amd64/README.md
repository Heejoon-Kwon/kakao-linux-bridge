# Wine 10 X11 clipboard driver

`winex11.so` is an experimental amd64 build of the Wine 10.0 X11 driver. The setup script limits it to Ubuntu Wine `10.0~repack-12ubuntu1` and keeps the system copy intact.

[Wine 10.0 source](https://github.com/wine-mirror/wine/tree/wine-10.0) and the [PNG clipboard patch](../../patches/wine-10-png-clipboard.patch) are the corresponding source for this module. Wine is distributed under the GNU Lesser General Public License, version 2.1 or later.

Build on amd64 with the Wine X11 developer dependencies, OpenGL, Vulkan and libpng available. Apply the patch in the Wine source tree, configure an out-of-tree build with `--enable-win64 --without-mingw --without-freetype --without-wayland`, then run `make dlls/winex11.drv/winex11.so`. The module is used with Ubuntu's existing runtime and its Windows-side DLLs.
