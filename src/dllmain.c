#include "kw_common.h"
#include "runtime.h"

BOOL WINAPI DllMainCRTStartup(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (!kw_runtime_attach(module, (kw_u8 *)GetModuleHandleW(NULL))) {
            OutputDebugStringA("kw_fps_patch: bootstrap validation/hook installation failed\n");
        }
    }
    return TRUE;
}
