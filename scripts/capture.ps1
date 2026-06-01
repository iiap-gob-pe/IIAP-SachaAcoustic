param([string]$keys = "", [string]$out = "cap.png", [int]$wait = 700)
Add-Type -AssemblyName System.Drawing
if (-not ("W.U" -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
namespace W { public class U {
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern IntPtr PostMessageA(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int l,t,r,b; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int x,y; }
}}
'@
}
$dir = "x:\biofonia\Paisaje_sonoro\analisis_espectograma_resaltar_frecuencias\etiquetador_cpp"
$p = Get-Process IIAP_SachaAcoustic -ErrorAction SilentlyContinue
if (-not $p) { "no raven en ejecucion"; exit }
$h = $p.MainWindowHandle
[W.U]::ShowWindow($h, 9) | Out-Null
[W.U]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
if ($keys -ne "") {
  foreach ($ch in $keys.ToCharArray()) {
    [W.U]::PostMessageA($h, 0x0102, [IntPtr][int][char]$ch, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 120
  }
}
Start-Sleep -Milliseconds $wait
$r = New-Object W.U+RECT;  [W.U]::GetClientRect($h, [ref]$r) | Out-Null
$tl = New-Object W.U+POINT; [W.U]::ClientToScreen($h, [ref]$tl) | Out-Null
$w = $r.r - $r.l; $hh = $r.b - $r.t
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($tl.x, $tl.y, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$bmp.Save("$dir\$out")
"OK $out ($w x $hh) keys='$keys'"
