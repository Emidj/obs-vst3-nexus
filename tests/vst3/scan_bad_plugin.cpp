// Real DLL entry-point failure fixtures, test-only; no SDK code or instrument.
#include <windows.h>
extern "C" __declspec(dllexport) bool InitDll(){return true;}
extern "C" __declspec(dllexport) bool ExitDll(){return true;}
extern "C" __declspec(dllexport) void* GetPluginFactory(){
#ifdef NEXUS_HANG_FIXTURE
    Sleep(INFINITE);
#else
    RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,0,nullptr);
#endif
    return nullptr;
}
