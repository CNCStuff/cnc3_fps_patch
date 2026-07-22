#include "kw_common.h"
#include "runtime.h"

BOOL WINAPI DllMainCRTStartup(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_kw_self_module = module;
        g_kw_game_module = (kw_u8 *)GetModuleHandleW(NULL);
        DisableThreadLibraryCalls(module);
        if (!kw_install_bootstrap_hooks()) {
            OutputDebugStringA("kw_fps_patch: bootstrap validation/hook installation failed\n");
        }
    }
    return TRUE;
}
