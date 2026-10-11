// Lo compila tools/oracles/tricnes.py junto con el núcleo de TriCNES (Emulator.cs y mappers/), sin su ventana.
// Igual que ramdump: tricnes_ramdump rom.nes cuadros [boton:desde-hasta ...] y vuelca los 2 KB de RAM.
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
using TriCNES;

// El núcleo dibuja en un System.Drawing.Bitmap (DirectBitmap), que en .NET solo existe en Windows. Aquí no se mira la
// imagen, así que basta con un Bitmap vacío: el núcleo compila y corre igual en Linux y macOS.
namespace System.Drawing
{
    public sealed class Bitmap : IDisposable
    {
        public Bitmap(int width, int height, int stride, Imaging.PixelFormat format, IntPtr scan0) { }
        public void Dispose() { }
    }
}
namespace System.Drawing.Imaging { public enum PixelFormat { Format32bppPArgb } }

static class TricnesRamdump
{
    static int Main(string[] args)
    {
        if (args.Length < 2) { Console.Error.WriteLine("uso: tricnes_ramdump rom.nes cuadros [boton:desde-hasta ...]"); return 2; }
        // ControllerPort1 va en orden A, B, Select, Start, Up, Down, Left, Right (A en el bit 7).
        string[] nombres = { "a", "b", "select", "start", "up", "down", "left", "right" };
        var botones = new List<int[]>();
        for (int i = 2; i < args.Length; i++)
        {
            Match m = Regex.Match(args[i], "^([a-z]+):([0-9]+)-([0-9]+)$");
            int k = m.Success ? Array.IndexOf(nombres, m.Groups[1].Value) : -1;
            if (k >= 0) botones.Add(new int[] { 0x80 >> k, int.Parse(m.Groups[2].Value), int.Parse(m.Groups[3].Value) });
        }
        Emulator emu = new Emulator();
        Cartridge cart = new Cartridge(args[0]);
        emu.Cart = cart;
        cart.Emu = emu;
        int cuadros = int.Parse(args[1]);
        for (int f = 0; f < cuadros; f++)
        {
            byte pad = 0;
            foreach (int[] b in botones) if (f >= b[1] && f <= b[2]) pad |= (byte)b[0];
            emu.ControllerPort1 = pad;
            emu._CoreFrameAdvance();
        }
        var sb = new StringBuilder();
        for (int a = 0; a < 0x800; a++) sb.AppendFormat("{0:X4} {1:X2}\n", a, emu.RAM[a]);
        Console.Out.Write(sb.ToString());
        return 0;
    }
}
