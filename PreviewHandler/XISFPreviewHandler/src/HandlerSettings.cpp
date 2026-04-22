// HandlerSettings.cpp - see header.
#include "HandlerSettings.h"
#include <windows.h>

namespace xisf {

bool IsPreviewHandlerEnabled()
{
    DWORD value = 1;
    DWORD cb = sizeof(value);
    LONG st = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\DennisPayne\\XISF Shell Extension",
        L"PreviewEnabled",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &cb);
    if (st != ERROR_SUCCESS) {
        return true;
    }
    return value != 0;
}

} // namespace xisf
