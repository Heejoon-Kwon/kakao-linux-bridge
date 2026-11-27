#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static const WCHAR system_link_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink";
static const WCHAR substitutes_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes";

static LONG add_link(HKEY key, const WCHAR *name, const WCHAR *link)
{
    DWORD size = 0;
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, name, NULL, &type, NULL, &size);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        return result;
    if (size > 65536 || size % sizeof(WCHAR) || (result == ERROR_SUCCESS && type != REG_MULTI_SZ))
        return ERROR_INVALID_DATA;

    size_t added = wcslen(link) + 1;
    size_t capacity = added * sizeof(WCHAR) + size + 2 * sizeof(WCHAR);
    WCHAR *combined = calloc(1, capacity);
    if (!combined)
        return ERROR_NOT_ENOUGH_MEMORY;
    WCHAR *existing = combined + added;

    if (size) {
        result = RegQueryValueExW(key, name, NULL, &type, (BYTE *)existing, &size);
        if (result != ERROR_SUCCESS || type != REG_MULTI_SZ) {
            free(combined);
            return result == ERROR_SUCCESS ? ERROR_INVALID_DATA : result;
        }
        for (const WCHAR *entry = existing; *entry; entry += wcslen(entry) + 1)
            if (_wcsicmp(entry, link) == 0) {
                free(combined);
                return ERROR_SUCCESS;
            }
    }

    memcpy(combined, link, added * sizeof(WCHAR));
    result = RegSetValueExW(key, name, 0, REG_MULTI_SZ, (BYTE *)combined,
                           (DWORD)(added * sizeof(WCHAR) + (size + sizeof(WCHAR))));
    free(combined);
    return result;
}

static LONG drop_old_substitute(const WCHAR *name)
{
    HKEY key;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, substitutes_key, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (result != ERROR_SUCCESS)
        return result;

    WCHAR data[128] = {0};
    DWORD size = sizeof(data) - sizeof(WCHAR);
    DWORD type;
    result = RegQueryValueExW(key, name, NULL, &type, (BYTE *)data, &size);
    if (result == ERROR_SUCCESS && type == REG_SZ &&
        (_wcsicmp(data, L"NanumGothic") == 0 || _wcsicmp(data, L"Noto Sans CJK KR") == 0))
        result = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return result == ERROR_FILE_NOT_FOUND || result == ERROR_MORE_DATA ? ERROR_SUCCESS : result;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc != 3 || !*argv[1] || !*argv[2] || wcspbrk(argv[1], L"\\/:,\r\n") ||
        wcspbrk(argv[2], L",\r\n"))
        return 2;
    WCHAR link[512];
    int length = swprintf(link, sizeof(link) / sizeof(*link), L"%ls,%ls", argv[1], argv[2]);
    if (length < 0 || length >= (int)(sizeof(link) / sizeof(*link)))
        return 2;
    HKEY key;
    LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, system_link_key, 0, NULL, 0,
                                  KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 3;
    result = add_link(key, L"Tahoma", link);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        return 4;
    return drop_old_substitute(L"Tahoma") == ERROR_SUCCESS ? 0 : 5;
}
