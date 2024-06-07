// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "vector"
#include "loader.h"

using namespace loader;

float target_height = 480;

void PatchBytes(uintptr_t address, const std::vector<BYTE>& bytes);
DWORD WINAPI DllThread(LPVOID hDll);
template<class T> T* ReadMultiLevelPointerSafe(void* base_address, const std::vector<uintptr_t>& offsets);

size_t find_last_of_substr(std::string toSearchFrom, const char* toFind) {
    size_t previousIndex = std::string::npos;
    size_t currIndex = std::string::npos;
    do {
        previousIndex = currIndex;
        currIndex = toSearchFrom.find(toFind, previousIndex + 1);
    } while (currIndex != std::string::npos);
    return previousIndex;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        LOG(INFO) << "Game version is " << GameVersion;

        // Start message
        LOG(DEBUG) << "Started LowResolutionMHW";

        // Check filename to see what percentage to set the thing to
        LOG(DEBUG) << "LowResolutionMHW: Checking if there's a valid res_scaling.";

        char dllFilePath[512+1];
        GetModuleFileNameA(hModule, dllFilePath, 512);
        std::string dllfilepath = dllFilePath;
        // Extract Filename
        std::string filename = dllfilepath;
        size_t filename_index = dllfilepath.find_last_of("/\\");
        if (filename_index != std::string::npos) {
            // Extract name after path delim
            filename = dllfilepath.substr(filename_index + 1, dllfilepath.length() - filename_index - 1);
        }
        LOG(DEBUG) << "LowResolutionMHW: Plugin filename is " << filename;
        size_t harg_index = find_last_of_substr(filename, "-h");
        if (harg_index != std::string::npos) {
            std::string harg = filename.substr(harg_index + 2, filename.length() - (harg_index + 2) - 4);
            LOG(INFO) << "LowResolutionMHW: H arg is " << harg;
            try {
                if (harg.length() > 0) {
                    target_height = std::stof(harg);
                    // Clamp the resolution scaling
                    if (target_height < 1) target_height = 1.0;
                    LOG(INFO) << "LowResolutionMHW: Using " << target_height << " as target_height ";
                }
                else {
                    LOG(ERR) << "LowResolutionMHW: Custom resolution is empty. Please remove any -h in the plugin filename if you intend to use the default plugin resolution, or just remove the plugin";
                    return false;
                }
            }
            catch (...) {
                LOG(ERR) << "LowResolutionMHW: Invalid custom resolution. Make sure that the -h argument is a valid number";
                return false;
            }
        }
        else {
            LOG(DEBUG) << "LowResolutionMHW: No H arg found";
        }

        // Create thread to lock resolution scaling
        CreateThread(nullptr, 0, DllThread, hModule, 0, nullptr);
        return true;
    }
}

DWORD WINAPI DllThread(LPVOID hDll)
{
    LOG(DEBUG) << "LowResolutionMHW: Thread Started. Waiting for res_scaling pointer to be valid";
    float* game_res_scale = ReadMultiLevelPointerSafe<float>((void*)0x1451C2240, { 0x1F0 });
    LOG(DEBUG) << "LowResolutionMHW: res_scaling pointer valid. Getting height dimension of the game";
    int* game_width = ReadMultiLevelPointerSafe<int>((void*)0x1451C2240, { 0x190 });
    int* game_height = ReadMultiLevelPointerSafe<int>((void*)0x1451C2240, { 0x194 });
    LOG(INFO) << "LowResolutionMHW: Game is running at "<<*game_width<<"x"<<*game_height;
    // Calculate res_scaling from the target height
    float res_scaling = target_height / *game_height;
    if (res_scaling > 1.0) {
        LOG(INFO) << "LowResolutionMHW: Target height (" << target_height << ") is bigger than game height (" << game_height << "). Plugin will stop";
    }
    else {
        LOG(INFO) << "LowResolutionMHW: Now locking the res_scaling value to " << res_scaling;

        // Patch logic that limits res scaling to be 0.5 to 1.0
        LOG(DEBUG) << "LowResolutionMHW: Patching resolution scale clamping code...";

        PatchBytes(0x142296F9B, { 0x0F, 0x2F, 0xC9 });
        PatchBytes(0x142296FAB, { 0x0F, 0x2F, 0xC9 });
        PatchBytes(0x142296FAE, { 0x77, 0x03 });

        PatchBytes(0x14255C328, { 0x0F, 0x2F, 0xC9 });
        PatchBytes(0x14255C333, { 0x77, 0x0A });
        PatchBytes(0x14255C335, { 0x0F, 0x2F, 0xC9 });
        PatchBytes(0x14255C338, { 0x73, 0x08 });
        PatchBytes(0x14255C352, { 0x0F, 0x2F, 0xC9 });
        PatchBytes(0x14255C355, { 0x77, 0x18 });
        PatchBytes(0x14255C357, { 0x0F, 0x2F, 0xC9 });

        LOG(DEBUG) << "LowResolutionMHW: Finished patching resolution scale clamping code!";

        int target_width = floor(*game_width * res_scaling);
        LOG(INFO) << "LowResolutionMHW: The game should be rendering the world at " << target_width << "x" << target_height;
        while (true) {
            *game_res_scale = res_scaling;
            Sleep(2000);
        }
    }
    FreeLibraryAndExitThread((HMODULE)hDll, 0);
    return 0;
}

template<class T>
T* ReadMultiLevelPointerSafe(void* base_address, const std::vector<uintptr_t>& offsets)
{
    uintptr_t addr = (uintptr_t)base_address;
    for (const auto& offset : offsets)
    {
        while (*(uintptr_t*)addr == NULL) Sleep(30);
        addr = *(uintptr_t*)addr;
        addr += offset;
    }
    return (T*)addr;
}

void PatchBytes(uintptr_t address, const std::vector<BYTE>& bytes) {
    DWORD oldProt = 0;
    VirtualProtect((LPVOID)address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProt);
    memcpy((void*)address, bytes.data(), bytes.size());
    VirtualProtect((LPVOID)address, bytes.size(), oldProt, &oldProt);
}