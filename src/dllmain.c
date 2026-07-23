#include "common.h"
#include "runtime.h"

BOOL WINAPI DllMainCRTStartup(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (!runtime_attach(module, (u8 *)GetModuleHandleW(NULL))) {
            OutputDebugStringA("fps_patch: bootstrap validation/hook installation failed\n");
        }
    }
    return TRUE;
}
