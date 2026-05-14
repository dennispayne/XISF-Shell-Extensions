using System.Runtime.InteropServices;

namespace XISFInstallerTests;

/// <summary>
/// COM / P/Invoke surface needed to drive the XISF Shell Extensions
/// handlers directly (no Explorer in the loop). Mirrors the PowerShell
/// recipes in the windows-sandbox-harness skill but stays pure C# so the
/// tests have no external script dependency.
/// </summary>
internal static class HandlerInterop
{
    public static readonly Guid IID_IShellItem            = new("43826D1E-E718-42EE-BC55-A1E261C37BFE");
    public static readonly Guid IID_IPropertyStore        = new("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
    public static readonly Guid IID_IShellItemImageFactory = new("BCC18B79-BA16-442F-80C4-8A59C30C463B");

    public const int SIIGBF_RESIZETOFIT   = 0x00;
    public const int SIIGBF_BIGGERSIZEOK  = 0x01;
    public const int SIIGBF_THUMBNAILONLY = 0x08;

    [DllImport("propsys.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    public static extern void PSGetItemPropertyHandler(
        IShellItem psi,
        bool fReadWrite,
        ref Guid riid,
        [MarshalAs(UnmanagedType.Interface)] out object ppv);

    [DllImport("shell32.dll", EntryPoint = "SHCreateItemFromParsingName",
        CharSet = CharSet.Unicode, PreserveSig = false)]
    public static extern IShellItem SHCreateItemFromParsingName_ShellItem(
        string pszPath, IntPtr pbc, ref Guid riid);

    [DllImport("shell32.dll", EntryPoint = "SHCreateItemFromParsingName",
        CharSet = CharSet.Unicode, PreserveSig = false)]
    public static extern IShellItemImageFactory SHCreateItemFromParsingName_Image(
        string pszPath, IntPtr pbc, ref Guid riid);

    [DllImport("query.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    [return: MarshalAs(UnmanagedType.IUnknown)]
    public static extern object LoadIFilter(
        string pwcsPath,
        [MarshalAs(UnmanagedType.IUnknown)] object? pUnkOuter,
        [MarshalAs(UnmanagedType.IUnknown)] out object ppIUnk);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteObject(IntPtr hObj);

    [ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IShellItem { }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROPERTYKEY
    {
        public Guid fmtid;
        public uint pid;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROPVARIANT
    {
        public ushort vt;
        public ushort r1, r2, r3;
        public IntPtr p1, p2;
    }

    [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IPropertyStore
    {
        uint GetCount();
        void GetAt(uint i, out PROPERTYKEY pk);
        void GetValue(ref PROPERTYKEY pk, out PROPVARIANT pv);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SIZE
    {
        public int cx;
        public int cy;
    }

    [ComImport, Guid("BCC18B79-BA16-442F-80C4-8A59C30C463B"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IShellItemImageFactory
    {
        void GetImage([In] SIZE size, [In] int flags, out IntPtr phbm);
    }

    /// <summary>
    /// Many shell extensions are registered with ThreadingModel=Apartment.
    /// Calling them from MTA works through a proxy but is more brittle than
    /// running the call on a dedicated STA thread, especially for chained
    /// shell helpers like SHCreateItemFromParsingName.
    /// </summary>
    public static T RunSta<T>(Func<T> body)
    {
        T result = default!;
        Exception? error = null;
        var thread = new Thread(() =>
        {
            try { result = body(); }
            catch (Exception ex) { error = ex; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (error is not null)
            throw new InvalidOperationException("STA work failed: " + error.Message, error);
        return result;
    }
}
