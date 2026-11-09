# Minimaler lokaler Webserver ohne Python (PowerShell). Beenden mit Strg+C oder Fenster schliessen.
# Web Serial / WebUSB brauchen einen "sicheren Kontext": http://localhost zaehlt dazu (file:// nicht).
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$mime = @{ '.html'='text/html; charset=utf-8'; '.js'='text/javascript; charset=utf-8'; '.json'='application/json'; '.bin'='application/octet-stream'; '.dfu'='application/octet-stream'; '.svg'='image/svg+xml'; '.png'='image/png'; '.css'='text/css'; '.txt'='text/plain; charset=utf-8'; '.md'='text/plain; charset=utf-8' }
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add('http://localhost:8000/')
try { $listener.Start() } catch {
  Write-Host 'FEHLER: Port 8000 ist belegt oder nicht erlaubt. Anderes Programm/Fenster mit Port 8000 schliessen und neu starten.' -ForegroundColor Red
  Write-Host $_.Exception.Message
  exit 1
}
Write-Host 'Viewer laeuft: http://localhost:8000   (Chrome oder Edge verwenden; Fenster offen lassen; Beenden mit Strg+C)'
Start-Process 'http://localhost:8000'
try {
  while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    try {
      $path = [System.Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath)
      if ($path -eq '/') { $path = '/index.html' }
      $file = Join-Path $root ($path.TrimStart('/').Replace('/', '\'))
      $full = [System.IO.Path]::GetFullPath($file)
      if ($full.StartsWith($root) -and (Test-Path $full -PathType Leaf)) {
        $bytes = [System.IO.File]::ReadAllBytes($full)
        $ext = [System.IO.Path]::GetExtension($full).ToLower()
        if ($mime.ContainsKey($ext)) { $ctx.Response.ContentType = $mime[$ext] } else { $ctx.Response.ContentType = 'application/octet-stream' }
        $ctx.Response.Headers.Add('Cache-Control', 'no-store')
        $ctx.Response.ContentLength64 = $bytes.Length
        $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
      } else {
        $ctx.Response.StatusCode = 404
      }
    } catch { Write-Host ('Anfrage-Fehler: ' + $_.Exception.Message) }
    try { $ctx.Response.OutputStream.Close() } catch {}
  }
} finally { $listener.Stop() }
