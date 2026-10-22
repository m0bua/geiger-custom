param(
    [Parameter(Mandatory = $true)]
    [string]$PortName
)

$ErrorActionPreference = 'Stop'

if (-not ('Geiger2NativeSerial' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class Geiger2NativeSerial
{
    [StructLayout(LayoutKind.Sequential)]
    public struct DCB
    {
        public uint DCBlength;
        public uint BaudRate;
        public uint Flags;
        public ushort wReserved;
        public ushort XonLim;
        public ushort XoffLim;
        public byte ByteSize;
        public byte Parity;
        public byte StopBits;
        public sbyte XonChar;
        public sbyte XoffChar;
        public sbyte ErrorChar;
        public sbyte EofChar;
        public sbyte EvtChar;
        public ushort wReserved1;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateFile(
        string fileName, uint desiredAccess, uint shareMode, IntPtr securityAttributes,
        uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetCommState(IntPtr handle, ref DCB dcb);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetCommState(IntPtr handle, ref DCB dcb);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool EscapeCommFunction(IntPtr handle, uint function);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CloseHandle(IntPtr handle);
}
'@
}

function Assert-NativeCall([bool]$Succeeded, [string]$Operation) {
    if (-not $Succeeded) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "$Operation failed (Win32 error $code)."
    }
}

$devicePath = "\\.\$PortName"
$handle = [Geiger2NativeSerial]::CreateFile(
    $devicePath,
    [uint32]::Parse('3221225472'), # GENERIC_READ | GENERIC_WRITE
    0,          # SerialPort v12 default: exclusive lock
    [IntPtr]::Zero,
    3,          # OPEN_EXISTING
    0x40000000, # FILE_FLAG_OVERLAPPED
    [IntPtr]::Zero
)

if ($handle -eq [IntPtr]::new(-1)) {
    $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "Could not open $PortName (Win32 error $code). Is another app using it?"
}

try {
    $dcb = New-Object Geiger2NativeSerial+DCB
    $dcb.DCBlength = [uint32][Runtime.InteropServices.Marshal]::SizeOf($dcb)
    Assert-NativeCall ([Geiger2NativeSerial]::GetCommState($handle, [ref]$dcb)) 'GetCommState'

    # Match the official app: SerialPort 12 defaults (hupcl=true, rtscts=false),
    # then its HiStand line format (115200 8E1), applied together via SetCommState.
    $dcb.BaudRate = 115200
    $dcb.ByteSize = 8
    $dcb.Parity = 2       # EVENPARITY
    $dcb.StopBits = 0     # ONESTOPBIT
    # fDtrControl=ENABLE (01b at bits 4..5); fRtsControl=DISABLE (00b).
    # Preserve all other flags, as the official native binding does.
    $dcb.Flags = ($dcb.Flags -band 0xFFFFFFCF) -bor 0x10
    $dcb.Flags = ($dcb.Flags -band 0xFFFFCFFF)
    Assert-NativeCall ([Geiger2NativeSerial]::SetCommState($handle, [ref]$dcb)) 'SetCommState'

    # Match the official app's post-open port.set({ rts:false, dtr:false }) order.
    Assert-NativeCall ([Geiger2NativeSerial]::EscapeCommFunction($handle, 4)) 'Clear RTS'
    Assert-NativeCall ([Geiger2NativeSerial]::EscapeCommFunction($handle, 6)) 'Clear DTR'

    Write-Host "Opened $PortName using the official native profile (115200 8E1)."
    Write-Host 'No UART bytes or reset/bootloader commands were sent.'
    Write-Host 'Observe the device for 10 seconds; press Enter to close the port.'
    Start-Sleep -Seconds 10
    [void](Read-Host 'Press Enter to close')
}
finally {
    [void][Geiger2NativeSerial]::CloseHandle($handle)
}
