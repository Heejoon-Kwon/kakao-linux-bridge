#include <windows.h>
#include <stdint.h>
#include <string.h>

static BOOL read_all(HANDLE input, void *data, DWORD size)
{
    BYTE *bytes = data;
    while (size) {
        DWORD received = 0;
        if (!ReadFile(input, bytes, size, &received, NULL) || !received)
            return FALSE;
        bytes += received;
        size -= received;
    }
    return TRUE;
}

static HGLOBAL read_part(HANDLE input, DWORD count, BOOL png)
{
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, count);
    if (!block)
        return NULL;
    void *bytes = GlobalLock(block);
    BOOL valid = bytes && read_all(input, bytes, count);
    if (valid && png) {
        static const BYTE signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        valid = count >= sizeof(signature) && !memcmp(bytes, signature, sizeof(signature));
    } else if (valid) {
        BITMAPINFOHEADER *header = bytes;
        valid = count >= sizeof(*header) && header->biSize == sizeof(*header) &&
                header->biWidth > 0 && header->biWidth <= 16384 &&
                header->biHeight > 0 && header->biHeight <= 16384 && header->biPlanes == 1 &&
                (header->biBitCount == 24 || header->biBitCount == 32) &&
                header->biCompression == BI_RGB;
        if (valid) {
            DWORD stride = (header->biWidth * (header->biBitCount / 8) + 3) & ~3u;
            valid = ((uint64_t)stride * header->biHeight) <= count - sizeof(*header);
        }
    }
    if (bytes)
        GlobalUnlock(block);
    if (!valid) {
        GlobalFree(block);
        return NULL;
    }
    return block;
}

int main(void)
{
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD sizes[2];
    if (!read_all(input, sizes, sizeof(sizes)) ||
        !sizes[0] || !sizes[1] || sizes[0] > 128u * 1024u * 1024u || sizes[1] > 128u * 1024u * 1024u)
        return 2;

    HGLOBAL bmp = read_part(input, sizes[0], FALSE);
    HGLOBAL png = read_part(input, sizes[1], TRUE);
    UINT png_format = RegisterClipboardFormatW(L"PNG");
    HWND owner = CreateWindowExW(0, L"STATIC", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE,
                                  NULL, GetModuleHandleW(NULL), NULL);
    int result = 3;
    if (bmp && png && png_format && owner) {
        for (unsigned attempt = 0; attempt < 20; ++attempt) {
            if (OpenClipboard(owner)) {
                if (EmptyClipboard() && SetClipboardData(CF_DIB, bmp)) {
                    bmp = NULL; /* Clipboard owns the bitmap after this call. */
                    result = 0;
                    if (SetClipboardData(png_format, png))
                        png = NULL;
                }
                CloseClipboard();
                break;
            }
            Sleep(20);
        }
    }
    if (owner)
        DestroyWindow(owner);
    if (bmp)
        GlobalFree(bmp);
    if (png)
        GlobalFree(png);
    return result;
}
