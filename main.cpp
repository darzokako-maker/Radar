#include <windows.h>
#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <vector>

// =========================================================================
// YÜKLEDİĞİNİZ OFSETLER (offsets.hpp)
// =========================================================================
namespace cs2_dumper {
    namespace offsets {
        namespace client_dll {
            constexpr std::ptrdiff_t dwEntityList = 0x24E5590;
            constexpr std::ptrdiff_t dwLocalPlayerController = 0x231E700;
            constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x233F698;
        }
    }
}

// =========================================================================
// ŞEMA VERİLERİ (client_dll.cs.txt)
// =========================================================================
namespace schemas {
    namespace client_dll {
        // C_BaseEntity / C_BasePlayerPawn altındaki değişkenler
        constexpr std::ptrdiff_t m_iHealth = 0x334;       // Can
        constexpr std::ptrdiff_t m_iTeamNum = 0x3E3;      // Takım (2: T, 3: CT)
        constexpr std::ptrdiff_t m_vOldOrigin = 0x1324;   // X, Y, Z Pozisyonu
        constexpr std::ptrdiff_t m_hPlayerPawn = 0x80C;   // Controller'dan Pawn'a bağlanan handle
        constexpr std::ptrdiff_t m_sSanitizedPlayerName = 0x770; // Oyuncu ismi adresi
    }
}

// 3 Boyutlu Vektör Yapısı (Mesafe Hesaplama İçin)
struct Vector3 {
    float x, y, z;
    float Mesafe(Vector3 hedef) {
        return std::sqrt(std::pow(hedef.x - x, 2) + std::pow(hedef.y - y, 2) + std::pow(hedef.z - z, 2));
    }
};

DWORD WINAPI RadarThread(LPVOID lpParam) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    std::cout << "========================================================\n";
    std::cout << "        CS2 ACTIVE ENTITY RADAR & DISTANCE TRACKER      \n";
    std::cout << "========================================================\n";
    std::cout << "[+] Dongu baslatildi. Cikis yapmak icin [END] basin.\n\n";

    uintptr_t clientModule = (uintptr_t)GetModuleHandleA("client.dll");
    if (!clientModule) {
        std::cout << "[-] client.dll bulunamadi!\n";
        if (f) fclose(f);
        FreeConsole();
        FreeLibraryAndExitThread((HMODULE)lpParam, 0);
        return 0;
    }

    while (!(GetAsyncKeyState(VK_END) & 0x8000)) {
        // Kendi karakterimizin bilgilerini alıyoruz
        uintptr_t localPlayerPawn = *(uintptr_t*)(clientModule + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        uintptr_t entityList = *(uintptr_t*)(clientModule + cs2_dumper::offsets::client_dll::dwEntityList);

        if (!localPlayerPawn || !entityList) {
            std::cout << "\r[-] Oyun icine girilmesi bekleniyor..." << std::flush;
            Sleep(500);
            continue;
        }

        // Kendi pozisyonumuz
        Vector3 localPos = *(Vector3*)(localPlayerPawn + schemas::client_dll::m_vOldOrigin);

        // Ekranı temizlemek yerine yukarı taşıma komutu (Konsolun titremesini önler)
        COORD cursorPosition;
        cursorPosition.X = 0;
        cursorPosition.Y = 5;
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), cursorPosition);

        std::cout << "--------------------------------------------------------\n";
        std::cout << " NO  | ISIM             | TAKIM | CAN  | MESAFE (Metre) \n";
        std::cout << "--------------------------------------------------------\n";

        int listelenenOyuncu = 0;

        // Max 64 oyuncuyu tarıyoruz
        for (int i = 1; i <= 64; i++) {
            // Source 2 Entity List erişim protokolü (List Entry / Node ayrıştırma)
            uintptr_t listEntry = *(uintptr_t*)(entityList + ((8 * (i & 0x7FFF) >> 9) + 16));
            if (!listEntry) continue;

            // Oyuncu Controller (Yönetici) nesnesi
            uintptr_t playerController = *(uintptr_t*)(listEntry + 120 * (i & 0x1FF));
            if (!playerController) continue;

            // Kendi kendimizi listede yazdırmamak için kontrol
            uintptr_t localController = *(uintptr_t*)(clientModule + cs2_dumper::offsets::client_dll::dwLocalPlayerController);
            if (playerController == localController) continue;

            // Controller üzerinden oyuncunun fiziksel gövdesine (Pawn) ulaşıyoruz
            uint32_t playerPawnHandle = *(uint32_t*)(playerController + schemas::client_dll::m_hPlayerPawn);
            if (!playerPawnHandle) continue;

            // Handle ID ile ikinci katman Entity listesinden gerçek Pawn adresini çekme
            uintptr_t listEntry2 = *(uintptr_t*)(entityList + (8 * ((playerPawnHandle & 0x1FFF) >> 9) + 16));
            if (!listEntry2) continue;

            uintptr_t playerPawn = *(uintptr_t*)(listEntry2 + 120 * (playerPawnHandle & 0x1FF));
            if (!playerPawn) continue;

            // Verileri hafızadan doğrudan güvenli bir şekilde okuyoruz
            int health = *(int*)(playerPawn + schemas::client_dll::m_iHealth);
            int team = *(int*)(playerPawn + schemas::client_dll::m_iTeamNum);
            Vector3 enemyPos = *(Vector3*)(playerPawn + schemas::client_dll::m_vOldOrigin);

            // Sadece canlı olan ve geçerli takımdaki (2 veya 3) oyuncuları göster
            if (health <= 0 || health > 100 || (team != 2 && team != 3)) continue;

            // Oyuncu ismini çekme (char dizisi pointerı üzerinden)
            char* namePtr = (char*)(playerController + schemas::client_dll::m_sSanitizedPlayerName);
            std::string playerName = (namePtr != nullptr && *namePtr != '\0') ? namePtr : "Bot / Oyuncu";
            if (playerName.length() > 15) playerName = playerName.substr(0, 12) + "...";

            // İki koordinat arasındaki ham birimi metreye çevirme (Source motorunda ~32 birim = 1 metredir)
            float mesafeBirim = localPos.Mesafe(enemyPos);
            float mesafeMetre = mesafeBirim / 32.0f;

            std::string takimStr = (team == 2) ? "T " : "CT";

            // Düzenli konsol çıktısı
            printf(" [#%02d] | %-16s |  %s   | %03d  | %.1f m\n", 
                   i, playerName.c_str(), takimStr.c_str(), health, mesafeMetre);
            
            listelenenOyuncu++;
        }

        if (listelenenOyuncu == 0) {
            std::cout << " [-] Etrafta aktif veya canli baska bir oyuncu tespit edilemedi.  \n";
        }

        // Konsolun altındaki eski yazıları temizlemek için boşluk bırakma
        std::cout << "--------------------------------------------------------\n";
        std::cout << "                                                        \n";
        std::cout << "                                                        \n";

        Sleep(250); // Ekranın yenilenme hızı (FPS kilidi)
    }

    std::cout << "\n[-] Radar kapatiliyor. Hafiza boşaltiliyor...\n";
    Sleep(1000);
    if (f) fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, RadarThread, hModule, 0, nullptr);
    }
    return TRUE;
}

