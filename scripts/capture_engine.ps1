# Motor de captura de figuras del manual. Define Run-Fig $name @("key:a","wait:900",...) y captura PNG.
# Relanza la app por figura (estado limpio: audio cargado, vista 2D, filtros por defecto).
param([string]$Wav = "x:\biofonia\Paisaje_sonoro\analisis_espectograma_resaltar_frecuencias\test_audio.wav")
$dir = "x:\biofonia\Paisaje_sonoro\analisis_espectograma_resaltar_frecuencias\etiquetador_cpp"
$exe = "$dir\IIAP_SachaAcoustic.exe"
$outImg = "$dir\docs"
Add-Type -AssemblyName System.Drawing
if (-not ("CAP.U" -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
namespace CAP { public class U {
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x,int y,int cx,int cy,uint f);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern IntPtr PostMessageA(IntPtr h, uint m, IntPtr w, IntPtr l);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int l,t,r,b; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int x,y; }
}}
'@
}
function LP([int]$x,[int]$y){ return [IntPtr]([long]($y*65536 + ($x -band 0xFFFF))) }
function Run-Fig([string]$name, [string[]]$steps){
  Remove-Item "$dir\out\test_audio.json" -ErrorAction SilentlyContinue   # estado limpio (sin auto-carga de etiquetas viejas)
  $st = Start-Process -FilePath $exe -ArgumentList $Wav,"$dir\out" -WorkingDirectory $dir -PassThru
  Start-Sleep -Seconds 2
  $h = $st.MainWindowHandle
  [CAP.U]::ShowWindow($h,9)|Out-Null
  [CAP.U]::SetWindowPos($h,[IntPtr]-1,0,0,1100,720,0x40)|Out-Null    # HWND_TOPMOST
  [CAP.U]::SetWindowPos($h,[IntPtr]-2,0,0,1100,720,0x40)|Out-Null    # HWND_NOTOPMOST (keep pos)
  [CAP.U]::BringWindowToTop($h)|Out-Null; [CAP.U]::SetForegroundWindow($h)|Out-Null
  Start-Sleep -Milliseconds 500
  $r = New-Object CAP.U+RECT; [CAP.U]::GetClientRect($h,[ref]$r)|Out-Null; $cW=$r.r-$r.l; $cH=$r.b-$r.t
  foreach($s in $steps){
    $p = $s.Split(':',2); $op=$p[0]; $arg=if($p.Count -gt 1){$p[1]}else{""}
    switch($op){
      'key'    { [CAP.U]::PostMessageA($h,0x0102,[IntPtr][int][char]$arg[0],[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 140 }
      'vkey'   { $vk=[Convert]::ToInt32($arg,16); [CAP.U]::PostMessageA($h,0x0100,[IntPtr]$vk,[IntPtr]0)|Out-Null; [CAP.U]::PostMessageA($h,0x0101,[IntPtr]$vk,[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 160 }
      'click'  { $f=$arg.Split(','); $x=[int]($cW*[double]$f[0]); $y=[int]($cH*[double]$f[1]); [CAP.U]::PostMessageA($h,0x0201,[IntPtr]1,(LP $x $y))|Out-Null; [CAP.U]::PostMessageA($h,0x0202,[IntPtr]0,(LP $x $y))|Out-Null; Start-Sleep -Milliseconds 140 }
      'rclick' { $f=$arg.Split(','); $x=[int]($cW*[double]$f[0]); $y=[int]($cH*[double]$f[1]); [CAP.U]::PostMessageA($h,0x0204,[IntPtr]2,(LP $x $y))|Out-Null; [CAP.U]::PostMessageA($h,0x0205,[IntPtr]0,(LP $x $y))|Out-Null; Start-Sleep -Milliseconds 200 }
      'move'   { $f=$arg.Split(','); $x=[int]($cW*[double]$f[0]); $y=[int]($cH*[double]$f[1]); [CAP.U]::PostMessageA($h,0x0200,[IntPtr]0,(LP $x $y))|Out-Null; Start-Sleep -Milliseconds 150 }
      'drag'   { $f=$arg.Split(','); $x0=[int]($cW*[double]$f[0]);$y0=[int]($cH*[double]$f[1]);$x1=[int]($cW*[double]$f[2]);$y1=[int]($cH*[double]$f[3]);
                 [CAP.U]::PostMessageA($h,0x0201,[IntPtr]1,(LP $x0 $y0))|Out-Null
                 for($t=1;$t -le 10;$t++){ $mx=[int]($x0+($x1-$x0)*$t/10);$my=[int]($y0+($y1-$y0)*$t/10); [CAP.U]::PostMessageA($h,0x0200,[IntPtr]1,(LP $mx $my))|Out-Null; Start-Sleep -Milliseconds 12 }
                 [CAP.U]::PostMessageA($h,0x0202,[IntPtr]0,(LP $x1 $y1))|Out-Null; Start-Sleep -Milliseconds 140 }
      'wait'   { Start-Sleep -Milliseconds ([int]$arg) }
    }
  }
  Start-Sleep -Milliseconds 350
  $tl = New-Object CAP.U+POINT; [CAP.U]::ClientToScreen($h,[ref]$tl)|Out-Null
  $bmp = New-Object System.Drawing.Bitmap $cW,$cH; $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($tl.x,$tl.y,0,0,(New-Object System.Drawing.Size($cW,$cH))); $bmp.Save("$outImg\$name")
  $g.Dispose(); $bmp.Dispose()
  Stop-Process -Id $st.Id -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 200
  "OK $name ($cW x $cH)"
}
"capture_engine cargado"
