using System;
using System.Threading;
using Swed64;

class Program
{
    [span_0](start_span)// Senin gönderdiğin dosyadaki güncel offsetler[span_0](end_span)
    static nint dwEntityList = 0x24D0DC0; 
    
    // Alt offsetler (Genelde şemalar üzerinden sabit kalır)
    static int m_hPlayerPawn = 0x7BC;
    static int m_iHealth = 0x32C;
    static int m_sSanitizedPlayerName = 0x720;

    static void Main()
    {
        // Videodaki Swed kütüphanesi başlatma
        Swed swed = new Swed("cs2");
        IntPtr client = swed.GetModuleBase("client.dll");

        Console.Title = "CS2 Entity Reader - Mayis 2026 Guncel";
        Console.WriteLine("Uygulama Baslatildi. Veriler okunuyor...");

        while (true)
        {
            // Entity List başlangıcını oku
            IntPtr entityList = swed.ReadPointer(client, (int)dwEntityList);

            for (int i = 0; i < 64; i++)
            {
                // 1. Giriş: Controller bulma
                IntPtr listEntry = swed.ReadPointer(entityList, 0x10);
                if (listEntry == IntPtr.Zero) continue;

                IntPtr currentController = swed.ReadPointer(listEntry, i * 0x78);
                if (currentController == IntPtr.Zero) continue;

                // İsim bilgisini Controller'dan alıyoruz
                string name = swed.ReadString(currentController, m_sSanitizedPlayerName, 16);
                
                // Pawn Handle al (İkinci girişe geçmek için)
                int pawnHandle = swed.ReadInt(currentController, m_hPlayerPawn);
                if (pawnHandle == 0) continue;

                // 2. Giriş: Videodaki meşhur Bitwise hesaplaması
                IntPtr listEntry2 = swed.ReadPointer(entityList, 0x8 * ((pawnHandle & 0x7FFF) >> 9) + 0x10);
                if (listEntry2 == IntPtr.Zero) continue;

                // Mevcut Pawn adresine ulaş ve Can (Health) oku
                IntPtr currentPawn = swed.ReadPointer(listEntry2, 0x78 * (pawnHandle & 0x1FF));
                if (currentPawn == IntPtr.Zero) continue;

                int health = swed.ReadInt(currentPawn, m_iHealth);

                // Sadece yaşayan oyuncuları göster
                if (health > 0 && health <= 100)
                {
                    Console.WriteLine($"[ID: {i}] Isim: {name.PadRight(15)} | Saglik: {health}");
                }
            }

            Thread.Sleep(500); // Ekranı saniyede 2 kez tazele
            Console.Clear();
            Console.WriteLine("--- CS2 Guncel Entity Listesi ---");
        }
    }
}

