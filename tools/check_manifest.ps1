param([string]$Exe = "build\routeperapp.exe")
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class R {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr LoadLibraryExW(string p, IntPtr h, uint flags);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr FindResourceW(IntPtr m, IntPtr name, IntPtr type);
  [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr m, IntPtr res);
  [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr data);
  [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr m, IntPtr res);
}
"@
$flags = 0x00000002  # LOAD_LIBRARY_AS_DATAFILE
$m = [R]::LoadLibraryExW((Resolve-Path $Exe).Path, [IntPtr]::Zero, $flags)
if ($m -eq [IntPtr]::Zero) { Write-Output "modul nicht ladenbar"; exit 1 }
$RT_MANIFEST = [IntPtr]24
foreach ($id in 1,2) {
  $res = [R]::FindResourceW($m, [IntPtr]$id, $RT_MANIFEST)
  if ($res -eq [IntPtr]::Zero) { continue }
  $size = [R]::SizeofResource($m, $res)
  $data = [R]::LoadResource($m, $res)
  $ptr = [R]::LockResource($data)
  $buf = New-Object byte[] $size
  [System.Runtime.InteropServices.Marshal]::Copy($ptr, $buf, 0, $size)
  $txt = [System.Text.Encoding]::UTF8.GetString($buf)
  Write-Output ("--- Manifest {0} in {1} ---" -f $id, $Exe)
  Write-Output $txt
}
