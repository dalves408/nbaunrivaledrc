// Request the discrete high-performance GPU on switchable-graphics laptops.
// NVIDIA Optimus and AMD PowerXpress inspect these exported DWORD symbols on
// the EXE (not a DLL) at process start before D3D device creation.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
