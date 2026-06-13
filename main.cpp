#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <iomanip>
#include <algorithm>

#include "offsets.hpp"
#include "client_dll.hpp"

HANDLE processHandle = nullptr;

// HATA ÇÖZÜMÜ: Ofsetleri constexpr yerine runtime hesaplanan ptid olarak tanımlıyoruz.
// Dumper hiyerarşisine tam uyumlu hale getirildi.
struct {
    std::ptrdiff_t m_iHealth = cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth;
    std::ptrdiff_t m_iTeamNum = cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum;
    std::ptrdiff_t m_hOwnerEntity = cs2_dumper::schemas::client_dll::C_BaseEntity::m_hOwnerEntity; // m_hController yerine güncellendi
    std::ptrdiff_t m_sSanitizedPlayerName = cs2_dumper::schemas::client_dll::CCSPlayerController::m_sSanitizedPlayerName;
} schema;

struct PlayerInfo {
    int index;
    int health;
    int team;
    std::string name;
};

template <typename T>
T Read(uintptr_t address) {
    T value{};
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), &value, sizeof(T), nullptr);
    return value;
}

std::string ReadString(uintptr_t address, size_t maxSize = 32) {
    char buffer[128] = { 0 };
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), buffer, (std::min)(maxSize, sizeof(buffer) - 1), nullptr);
    return std::string(buffer);
}

// ... (GetProcessId ve GetModuleBaseAddress fonksiyonları aynı kalacak) ...

int main() {
    // ... (SetupConsole ve Process/Module bağlantı kodları aynı kalacak) ...

    while (true) {
        if (GetAsyncKeyState(VK_END) & 0x8000) break;

        uintptr_t entityList = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwEntityList);
        uintptr_t localPlayerPawn = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        
        if (entityList && localPlayerPawn) {
            std::vector<PlayerInfo> currentPlayers;
            int localTeam = Read<int>(localPlayerPawn + schema.m_iTeamNum);

            for (int i = 1; i < 64; i++) {
                uintptr_t listEntry = Read<uintptr_t>(entityList + (8 * (i >> 9) + 16));
                if (!listEntry) continue;
                
                uintptr_t currentPawn = Read<uintptr_t>(listEntry + 120 * (i & 0x1FF));
                if (!currentPawn || currentPawn == localPlayerPawn) continue;
                
                int health = Read<int>(currentPawn + schema.m_iHealth);
                int team = Read<int>(currentPawn + schema.m_iTeamNum);
                
                if (health > 0 && health <= 100 && (team == 2 || team == 3)) {
                    std::string playerName = "Unknown";
                    // m_hOwnerEntity üzerinden controller'a erişim (m_hController yerine)
                    uint32_t controllerHandle = Read<uint32_t>(currentPawn + schema.m_hOwnerEntity);
                    if (controllerHandle != 0xFFFFFFFF) {
                        uint32_t ctrlIndex = controllerHandle & 0x1FFF;
                        uintptr_t ctrlEntry = Read<uintptr_t>(entityList + (8 * (ctrlIndex >> 9) + 16));
                        if (ctrlEntry) {
                            uintptr_t controller = Read<uintptr_t>(ctrlEntry + 120 * (ctrlIndex & 0x1FF));
                            if (controller) {
                                playerName = ReadString(controller + schema.m_sSanitizedPlayerName);
                            }
                        }
                    }
                    currentPlayers.push_back({ i, health, team, playerName });
                }
            }
            // DisplayPlayers(currentPlayers, localTeam); // Görüntüleme fonksiyonu
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return 0;
}
