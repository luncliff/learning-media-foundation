#include "module.g.cpp"
#include <Hstring.h>
#include <activation.h>

HMODULE g_module = NULL;

BOOL APIENTRY DllMain(HANDLE handle, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_module = static_cast<HMODULE>(handle);
        break;
    default:
        break;
    }
    return TRUE;
}

// STDAPI DllCanUnloadNow() {
//     return WINRT_CanUnloadNow();
// }

// HRESULT WINAPI DllGetActivationFactory(_In_ HSTRING classid, _Out_ IActivationFactory** factory) {
//     return WINRT_GetActivationFactory(classid, (void**)factory);
// }
