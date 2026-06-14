#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resource.h"
#include "icon_loader.hpp"

IconBytes GetIconPNGBytes() {
    HMODULE hMod     = GetModuleHandle(NULL);
    HRSRC   hResInfo = FindResource(hMod, MAKEINTRESOURCE(IDI_PNG1), RT_RCDATA);
    if (!hResInfo) return { nullptr, 0 };
    HGLOBAL hRes  = LoadResource(hMod, hResInfo);
    DWORD   size  = SizeofResource(hMod, hResInfo);
    void*   pData = LockResource(hRes);
    return { (const unsigned char*)pData, (int)size };
}