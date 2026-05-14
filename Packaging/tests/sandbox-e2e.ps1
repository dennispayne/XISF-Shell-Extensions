<#
.SYNOPSIS
    End-to-end COM exercise of all four XISF Shell Extensions handlers
    against a local .xisf file. Designed to run inside a Windows Sandbox
    after the MSI has been installed.

.DESCRIPTION
    Copies a sample .xisf file to a local RW path (C:\LocalAstro), restarts
    Explorer to clear handler caches, then invokes:

      (1) Shell.Application.GetDetailsOf     - drives the property handler
                                                via the shell column system;
                                                expects ~58 populated columns
                                                including Astro * columns.
      (2) PSGetItemPropertyHandler            - exercises IPropertyStore
                                                directly via propsys; expects
                                                ~33 properties under our
                                                pkey GUID 7c54fa8b-... .
      (3) IShellItemImageFactory::GetImage    - exercises ThumbnailProvider;
                                                expects an aspect-correct
                                                bitmap (not the generic 256x256
                                                placeholder) saved as PNG.

    Writes per-test output to C:\Trace\e2e-<tag>-<timestamp>.txt and
    thumbnails to C:\Trace\thumb-<tag>-*.png.

.NOTES
    Files on Sandbox RO-mapped folders (those mapped from the host without
    --allow-write) trigger ERROR_OPLOCK_NOT_GRANTED (0x8007012C) when the
    property handler tries to take an oplock. Always copy the sample to a
    local RW location before exercising the handlers.

    Explorer caches handler registrations aggressively; if a previous MSI
    install set things up wrong, just installing the right MSI won't make
    handlers reappear until Explorer is restarted. This script restarts
    Explorer at the top of each run.
#>

[CmdletBinding()] param(
    [string]$Tag = 'local',
    [string]$Source = 'C:\astro'
)
$ErrorActionPreference = 'Continue'
$stamp = Get-Date -Format yyyyMMdd-HHmmss
$out = "C:\Trace\e2e-$Tag-$stamp.txt"
New-Item -ItemType Directory -Path 'C:\Trace' -Force | Out-Null
New-Item -ItemType Directory -Path 'C:\LocalAstro' -Force | Out-Null
function W($m) { $m | Out-File $out -Append }

W "===== E2E LOCAL TAG: $Tag    TIME: $stamp ====="

$src = Get-ChildItem -Path $Source -Recurse -Filter '*.xisf' -ErrorAction SilentlyContinue |
       Sort-Object Length | Select-Object -First 1
if (-not $src) { W "no .xisf found under $Source"; exit 1 }
$dst = Join-Path 'C:\LocalAstro' $src.Name
Copy-Item $src.FullName $dst -Force
W "Copied $($src.Length) bytes: $($src.FullName) -> $dst"

W ""
W "Restarting Explorer (clears handler cache)..."
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep 2
Start-Process explorer
Start-Sleep 3
W "Explorer restarted"
W ""

W "----- (1) GetDetailsOf -----"
try {
    $shell = New-Object -ComObject Shell.Application
    $folder = $shell.NameSpace((Split-Path $dst -Parent))
    $item = $folder.ParseName((Split-Path $dst -Leaf))
    $count = 0
    for ($col = 0; $col -lt 400; $col++) {
        $name = $folder.GetDetailsOf($null, $col)
        $val  = $folder.GetDetailsOf($item, $col)
        if ([string]::IsNullOrWhiteSpace($name)) { continue }
        if (-not [string]::IsNullOrWhiteSpace($val)) {
            W ("  col={0,3}  '{1}' = '{2}'" -f $col, $name, $val)
            $count++
        }
    }
    W "  -> $count populated columns"
} catch { W "  EX: $_" }
W ""

W "----- (2) IPropertyStore -----"
$src1 = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PropTest {
    [DllImport("propsys.dll", CharSet=CharSet.Unicode, PreserveSig=false)]
    static extern void PSGetItemPropertyHandler(IShellItem psi, bool fReadWrite, ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out object ppv);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode, PreserveSig=false)]
    static extern IShellItem SHCreateItemFromParsingName(string pszPath, IntPtr pbc, ref Guid riid);
    static readonly Guid IID_IShellItem    = new Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE");
    static readonly Guid IID_IPropertyStore = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
    [ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IShellItem {}
    [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IPropertyStore {
        uint GetCount();
        void GetAt(uint i, out PROPERTYKEY pk);
        void GetValue(ref PROPERTYKEY pk, out PROPVARIANT pv);
    }
    [StructLayout(LayoutKind.Sequential)] public struct PROPERTYKEY { public Guid fmtid; public uint pid; }
    [StructLayout(LayoutKind.Sequential)] public struct PROPVARIANT { public ushort vt; public ushort r1,r2,r3; public IntPtr p1, p2; }
    public static string Run(string path) {
        var sb = new StringBuilder();
        try {
            var iidShellItem = IID_IShellItem;
            var iidPS = IID_IPropertyStore;
            var item = SHCreateItemFromParsingName(path, IntPtr.Zero, ref iidShellItem);
            object pv;
            PSGetItemPropertyHandler(item, false, ref iidPS, out pv);
            var ps = (IPropertyStore)pv;
            uint n = ps.GetCount();
            sb.AppendLine("  PropertyStore returned " + n + " properties");
            for (uint i = 0; i < n; i++) {
                PROPERTYKEY pk; ps.GetAt(i, out pk);
                sb.AppendLine("    pkey " + i + ": " + pk.fmtid + " pid=" + pk.pid);
            }
        } catch (Exception e) { sb.AppendLine("  EX: " + e.GetType().Name + ": " + e.Message); }
        return sb.ToString();
    }
}
"@
try { Add-Type -TypeDefinition $src1 -ErrorAction Stop; W ([PropTest]::Run($dst)) } catch { W "  COMPILE: $_" }
W ""

W "----- (3) Thumbnail (save PNG) -----"
$src3 = @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
public static class ThumbTest {
    [DllImport("shell32.dll", CharSet=CharSet.Unicode, PreserveSig=false)]
    static extern IShellItemImageFactory SHCreateItemFromParsingName(string path, IntPtr bind, ref Guid riid);
    static readonly Guid IID_IShellItemImageFactory = new Guid("BCC18B79-BA16-442F-80C4-8A59C30C463B");
    [ComImport, Guid("BCC18B79-BA16-442F-80C4-8A59C30C463B"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IShellItemImageFactory {
        void GetImage([In] SIZE size, [In] int flags, out IntPtr phbm);
    }
    [StructLayout(LayoutKind.Sequential)] public struct SIZE { public int cx, cy; }
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr hObj);
    public const int SIIGBF_THUMBNAILONLY = 0x08;
    public static string Run(string path, string outPng, int flags) {
        var sb = new StringBuilder();
        try {
            var iid = IID_IShellItemImageFactory;
            var fac = SHCreateItemFromParsingName(path, IntPtr.Zero, ref iid);
            IntPtr hbm;
            fac.GetImage(new SIZE{cx=256,cy=256}, flags, out hbm);
            if (hbm != IntPtr.Zero) {
                using (var bmp = Bitmap.FromHbitmap(hbm)) {
                    sb.AppendLine("  flags=0x" + flags.ToString("X") + " -> " + bmp.Width + "x" + bmp.Height + " " + bmp.PixelFormat);
                    bmp.Save(outPng, System.Drawing.Imaging.ImageFormat.Png);
                    sb.AppendLine("  Saved: " + outPng);
                }
                DeleteObject(hbm);
            } else sb.AppendLine("  flags=0x" + flags.ToString("X") + " GetImage returned null HBITMAP");
            Marshal.ReleaseComObject(fac);
        } catch (Exception e) { sb.AppendLine("  EX flags=0x" + flags.ToString("X") + ": " + e.GetType().Name + ": " + e.Message); }
        return sb.ToString();
    }
}
"@
try {
    Add-Type -TypeDefinition $src3 -ReferencedAssemblies 'System.Drawing' -ErrorAction Stop
    W ([ThumbTest]::Run($dst, "C:\Trace\thumb-$Tag-default-$stamp.png", 0))
    W ([ThumbTest]::Run($dst, "C:\Trace\thumb-$Tag-thumbonly-$stamp.png", 0x08))
} catch { W "  COMPILE: $_" }

W ""
W "===== DONE ====="
"DONE: $out"
