// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "vector"
#include "loader.h"

#ifndef LRR_TARGET_HEIGHT
#define LRR_TARGET_HEIGHT 480
#endif

#define EXPECTED_GAME_VERSION "421810"

using namespace loader;

float target_height = LRR_TARGET_HEIGHT;

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
        if (strcmp(GameVersion, EXPECTED_GAME_VERSION) != 0) {
            LOG(ERR) << "LowResolutionMHW: Game version mismatch, the game must have been updated, please update the plugin! (game v.: " << GameVersion << ", expected v.: " << EXPECTED_GAME_VERSION << ")";
            return false;
        }

        // Start message
        LOG(DEBUG) << "Started LowResolutionMHW";
        LOG(INFO) << "LowResolutionMHW: Running the plugin at default " << target_height << " height";

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
    float* game_res_scale = ReadMultiLevelPointerSafe<float>((void*)0x1451C4480, { 0x1F0 });
    LOG(DEBUG) << "LowResolutionMHW: res_scaling pointer valid. (" << std::hex << std::showbase << reinterpret_cast<uintptr_t>(game_res_scale) << ") Getting height dimension of the game";
    int* game_width = ReadMultiLevelPointerSafe<int>((void*)0x1451C4480, { 0x190 });
    LOG(DEBUG) << "LowResolutionMHW: width pointer valid. (" << std::hex << std::showbase << reinterpret_cast<uintptr_t>(game_width) << ")";
    int* game_height = ReadMultiLevelPointerSafe<int>((void*)0x1451C4480, { 0x194 });
    LOG(DEBUG) << "LowResolutionMHW: height pointer valid. (" << std::hex << std::showbase << reinterpret_cast<uintptr_t>(game_height) << ")";
    while (*game_width < 1 && *game_height < 1) {
        LOG(DEBUG) << "LowResolutionMHW: width or height value was invalid. Retrying in 1s...";
        Sleep(1000);
    }
    LOG(INFO) << "LowResolutionMHW: Game is running at " << *game_width << "x" << *game_height;
    // Calculate res_scaling from the target height
    float res_scaling = target_height / *game_height;
    if (res_scaling > 1.0) {
        LOG(INFO) << "LowResolutionMHW: Target height (" << target_height << ") is bigger than game height (" << *game_height << "). Plugin will stop";
    }
    else {
        LOG(INFO) << "LowResolutionMHW: Now locking the res_scaling value to " << res_scaling;

        // Patch logic that limits res scaling to be 0.5 to 1.0
        LOG(DEBUG) << "LowResolutionMHW: Patching resolution scale clamping code...";

        // Master Resolution Scaling Clamp Patch
        // - Allows going below 50% resolution scale

        // 0x 142296F9B  ->  0x 142296FFB  ->  0x 14229837B 
        uintptr_t offset = 0x14229837B;
        uintptr_t localOffset = 0;
        PatchBytes(offset, { 0x0F, 0x2F, 0xC9 });
        PatchBytes(offset + (localOffset += 16), {0x0F, 0x2F, 0xC9}); // 16 bytes after (13 if after last)
        PatchBytes(offset + (localOffset += 3), { 0x77, 0x03 }); // 3 bytes after (just after the last command)

        // 0x 14255C328  ->  0x 14255C388  ->  0x 14255D708 (0x 2C538D from start of last block)
        offset += 0x2C538D;
        localOffset = 0;
        PatchBytes(offset, { 0x0F, 0x2F, 0xC9 }); // 0x 2C538D distance from last block start
        PatchBytes(offset + (localOffset += 11), { 0x77, 0x0A }); // 11 bytes (8 if after last)
        PatchBytes(offset + (localOffset += 2), { 0x0F, 0x2F, 0xC9 }); // just after
        PatchBytes(offset + (localOffset += 3), { 0x73, 0x08 }); // just after
        PatchBytes(offset + (localOffset += 26), { 0x0F, 0x2F, 0xC9 }); // 26 bytes (24 if after last)
        PatchBytes(offset + (localOffset += 3), { 0x77, 0x18 }); // just after
        PatchBytes(offset + (localOffset += 2), { 0x0F, 0x2F, 0xC9 }); // just after

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