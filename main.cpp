#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <iomanip>
#include <algorithm> // std::min fonksiyonu için zorunlu

// Güncel offsetler (Proje klasöründe mevcut olmalıdır)
#include "offsets.hpp"
#include "client_dll.hpp"

HANDLE processHandle = nullptr;

// Oyuncu ve takım verilerini tutan yapı
struct PlayerInfo {
    int index;
    int health;
    int team;
    std::string name;
};

// Güvenli bellek okuma şablonu
template <typename T>
T Read(uintptr_t address) {
    T value{};
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), &value, sizeof(T), nullptr);
    return value;
}

// String okuma (Bellek sınırı güvenli)
std::string ReadString(uintptr_t address, size_t maxSize = 32) {
    char buffer[128] = { 0 };
    // min yerine standart std::min kullanıyoruz ve bellek taşmasını engelliyoruz
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), buffer, (std::min)(maxSize, sizeof(buffer) - 1), nullptr);
    return std::string(buffer);
}

// Process ID bulma fonksiyonu
DWORD GetProcessId(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, processName) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

// Modül base adresini bulma fonksiyonu
uintptr_t GetModuleBaseAddress(DWORD pid, const wchar_t* moduleName) {
    uintptr_t baseAddress = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = { sizeof(MODULEENTRY32W) };
        if (Module32FirstW(snapshot, &me)) {
            do {
                if (_wcsicmp(me.szModule, moduleName) == 0) {
                    baseAddress = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snapshot, &me));
        }
        CloseHandle(snapshot);
    }
    return baseAddress;
}

// Konsol başlangıç ayarları
void SetupConsole() {
    SetConsoleTitle(L"CS2 Radar v1.1 Optimized");
    
    // Yanıp sönen imleci gizle
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(consoleHandle, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(consoleHandle, &cursorInfo);
    
    // Konsol boyutunu sabitle
    system("mode con: cols=65 lines=30");
}

// Verileri ekrana basan fonksiyon (Titreme ve hayalet satır engelli)
void DisplayPlayers(const std::vector<PlayerInfo>& players, int localTeam) {
    // İmleci temizlemeden sol üste çek (Titremeyi önler)
    COORD cursorPos = { 0, 0 };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), cursorPos);
    
    std::cout << "===============================================================\n";
    std::cout << " ID  | TAKIM  | CAN    | OYUNCU ADI\n";
    std::cout << "===============================================================\n";
    
    int printedLines = 0;
    for (const auto& p : players) {
        std::string teamStr;
        if (localTeam == 2 || localTeam == 3) {
            teamStr = (p.team == localTeam) ? "DOST " : "RAKIP";
        } else {
            teamStr = (p.team == 2) ? "T-TAK" : "CT-TAK";
        }
        
        // Sabit sütun genişliği için manipülatörleri her satırda sıfırlayarak kullanıyoruz
        std::cout << " [" << std::setw(2) << std::setfill('0') << p.index << "] | "
                  << std::setfill(' ') << std::setw(5) << teamStr << "  | "
                  << std::setw(3) << p.health << " HP | "
                  << std::left << std::setw(25) << p.name << std::right << "\n";
        printedLines++;
    }
    
    // Hayalet satırları temizleme: Eski döngüden kalan artıkları boşluk basarak temizler
    for (int k = printedLines; k < 20; k++) {
        std::cout << "                                                               \n";
    }
    
    std::cout << "===============================================================\n";
    std::cout << "Aktif Oyuncu Sayisi: " << std::setw(2) << players.size() << "                                \n";
}

int main() {
    SetupConsole();
    
    std::cout << "[*] CS2 (cs2.exe) bekleniyor..." << std::endl;
    
    DWORD pid = 0;
    while (pid == 0) {
        pid = GetProcessId(L"cs2.exe");
        if (pid == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    std::cout << "[+] CS2 Bulundu! PID: " << pid << std::endl;
    
    // Optimum yetkilendirme ile handle açma (Gereksiz yetkiler kaldırıldı)
    processHandle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!processHandle) {
        std::cout << "[-] Erisim engellendi! Programi Yonetici olarak calistirin." << std::endl;
        system("pause");
        return 1;
    }
    
    uintptr_t clientModule = GetModuleBaseAddress(pid, L"client.dll");
    if (!clientModule) {
        std::cout << "[-] client.dll bulunamadi!" << std::endl;
        CloseHandle(processHandle);
        system("pause");
        return 1;
    }
    
    std::cout << "[+] client.dll adresi: 0x" << std::hex << clientModule << std::dec << std::endl;
    std::cout << "[*] Radar baslatiliyor..." << std::endl;
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    system("cls"); // İlk çizim öncesi konsolu temizle
    
    // Ana Döngü
    while (true) {
        // Çıkış mekanizması (Konsolda END tuşuna basarak güvenli kapatma)
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            break;
        }

        uintptr_t entityList = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwEntityList);
        uintptr_t localController = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwLocalPlayerController);
        
        if (entityList && localController) {
            std::vector<PlayerInfo> currentPlayers;
            int localTeam = 0;

            // 1. KISIM: Kendi (Local) takım bilgisini tek seferde oku
            uint32_t localPawnHandle = Read<uint32_t>(localController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);
            if (localPawnHandle) {
                // DOĞRU DİZİN MASKESİ: Source 2 motoru için 0x1FFF kullanılmalıdır
                uint32_t localPawnIndex = localPawnHandle & 0x1FFF;
                uintptr_t localListEntry = Read<uintptr_t>(entityList + 8 * (localPawnIndex >> 9) + 16);
                if (localListEntry) {
                    uintptr_t localPawn = Read<uintptr_t>(localListEntry + 120 * (localPawnIndex & 0x1FF));
                    if (localPawn) {
                        localTeam = Read<int>(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
                    }
                }
            }

            // 2. KISIM: Diğer oyuncuları tara
            for (int i = 1; i <= 64; i++) {
                uintptr_t listEntry1 = Read<uintptr_t>(entityList + 8 * (i >> 9) + 16);
                if (!listEntry1) continue;
                
                uintptr_t playerController = Read<uintptr_t>(listEntry1 + 120 * (i & 0x1FF));
                if (!playerController || playerController == localController) continue;
                
                uint32_t pawnHandle = Read<uint32_t>(playerController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);
                if (!pawnHandle) continue;
                
                // DOĞRU DİZİN MASKESİ: Oyuncu taramasında da 0x1FFF maskesi uygulandı
                uint32_t pawnIndex = pawnHandle & 0x1FFF;
                uintptr_t listEntry2 = Read<uintptr_t>(entityList + 8 * (pawnIndex >> 9) + 16);
                if (!listEntry2) continue;
                
                uintptr_t playerPawn = Read<uintptr_t>(listEntry2 + 120 * (pawnIndex & 0x1FF));
                if (!playerPawn) continue;
                
                int health = Read<int>(playerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
                int team = Read<int>(playerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
                std::string name = ReadString(playerController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_sSanitizedPlayerName);
                
                // Senin yazdığın o kusursuz filtreleme kuralları
                if (health > 0 && health <= 100 && (team == 2 || team == 3) && !name.empty()) {
                    currentPlayers.push_back({i, health, team, name});
                }
            }
            
            // Ekrana bas
            DisplayPlayers(currentPlayers, localTeam);
        } else {
            COORD cursorPos = { 0, 0 };
            SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), cursorPos);
            std::cout << "Oyun verileri bekleniyor (Maca girilmesi gerek)...          " << std::endl;
        }
        
        // İşlemciyi yormamak için 30ms bekleme (Yaklaşık ~33 FPS güncelleme hızı)
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    
    // Güvenli kapanış (Zombie Handle oluşumunu engeller)
    if (processHandle) {
        CloseHandle(processHandle);
    }
    return 0;
}
