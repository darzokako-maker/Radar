using System;
using System.Threading;
using Swed64;

namespace CS2EntityReader
{
    class Program
    {
        // Guncel Offsetler
        static nint dwEntityList = 0x24D0DC0; 
        static int m_hPlayerPawn = 0x7BC;
        static int m_iHealth = 0x32C;
        static int m_sSanitizedPlayerName = 0x720;

        static void Main(string[] args)
        {
            // Swed64 baslatma
            Swed swed = new Swed("cs2");

            // Client.dll modulunu bul
            IntPtr client = swed.GetModuleBase("client.dll");

            Console.WriteLine("CS2 Entity List Reader Calisiyor...");

            while (true)
            {
                // Entity List pointer oku
                IntPtr entityList = swed.ReadPointer(client, (int)dwEntityList);

                for (int i = 0; i < 64; i++)
                {
                    // 1. Liste Girisi (Controller)
                    IntPtr listEntry = swed.ReadPointer(entityList, 0x10);
                    if (listEntry == IntPtr.Zero) continue;

                    IntPtr currentController = swed.ReadPointer(listEntry, i * 0x78);
                    if (currentController == IntPtr.Zero) continue;

                    // Isim bilgisini oku
                    string playerName = swed.ReadString(currentController, m_sSanitizedPlayerName, 16);

                    // Pawn Handle bilgisini oku
                    int pawnHandle = swed.ReadInt(currentController, m_hPlayerPawn);
                    if (pawnHandle == 0) continue;

                    // 2. Liste Girisi (Pawn Hesaplama - Videodaki Bitwise Formulu)
                    IntPtr listEntry2 = swed.ReadPointer(entityList, 0x8 * ((pawnHandle & 0x7FFF) >> 9) + 0x10);
                    if (listEntry2 == IntPtr.Zero) continue;

                    // Nihai Pawn adresi
                    IntPtr currentPawn = swed.ReadPointer(listEntry2, 0x78 * (pawnHandle & 0x1FF));
                    if (currentPawn == IntPtr.Zero) continue;

                    // Can bilgisini oku
                    int health = swed.ReadInt(currentPawn, m_iHealth);

                    if (health > 0 && health <= 100)
                    {
                        Console.WriteLine($"[ID: {i}] Isim: {playerName} | Saglik: {health}");
                    }
                }
                // Dongeyi yavaslat ve ekrani temizle
                Thread.Sleep(500);
                Console.Clear();
            }
        }
    }
}
