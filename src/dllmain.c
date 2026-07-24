#include "common.h"
#include "runtime.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /*
         * This executes under the loader lock. runtime_attach only validates
         * the image and installs two bootstrap branches; file I/O, allocation,
         * and live timing writes are deferred to those hooks.
         */
        if (!runtime_attach(module, (u8 *)GetModuleHandleW(NULL))) {
            OutputDebugStringA("fps_patch: bootstrap validation/hook installation failed\n");
        }
    }
    /* Unsupported games must still be able to use the DirectInput proxy. */
    return TRUE;
}
