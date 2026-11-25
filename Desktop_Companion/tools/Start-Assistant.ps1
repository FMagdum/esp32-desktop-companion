param([int]$Port = 9870)
$ErrorActionPreference = 'Stop'
if ($Port -lt 1 -or $Port -gt 65535) { throw 'Port must be between 1 and 65535.' }
$systemPython = (Get-Command python -ErrorAction Stop).Source
& $systemPython -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.10 or later is required.' }
$assistantDir = Join-Path $env:LOCALAPPDATA 'DesktopCompanion'
$tokenPath = Join-Path $assistantDir 'assistant-token.txt'
if (-not (Test-Path -LiteralPath $tokenPath -PathType Leaf)) {
  throw 'Bridge token not found. Run Configure-Assistant.ps1 with your microSD card first.'
}

function Start-YtmDesktop {
  $running = Get-Process -Name 'youtube-music-desktop-app', 'YouTube Music Desktop App' -ErrorAction SilentlyContinue
  if ($running) {
    Write-Host 'YTMDesktop is already running.'
    return
  }

  $candidates = @(
    (Join-Path $env:LOCALAPPDATA 'youtube_music_desktop_app\youtube-music-desktop-app.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\youtube-music-desktop-app\YouTube Music Desktop App.exe'),
    (Join-Path $env:ProgramFiles 'YouTube Music Desktop App\YouTube Music Desktop App.exe')
  )
  $executable = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
  if (-not $executable) {
    Write-Warning 'YTMDesktop was not found. Open it manually to use Pod music controls.'
    return
  }
  try {
    Start-Process -FilePath $executable -WindowStyle Hidden -ErrorAction Stop
    Write-Host 'Started YTMDesktop for Pod music controls.'
  } catch {
    Write-Warning 'Could not start YTMDesktop. Open it manually to use Pod music controls.'
  }
}

Start-YtmDesktop

$venv = Join-Path $assistantDir 'venv'
$python = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
  Write-Host 'Creating a private Python environment for the assistant...'
  & $systemPython -m venv $venv
  if ($LASTEXITCODE -ne 0) { throw 'Could not create the private Python environment.' }
}
& $python -c 'import importlib.util,sys; sys.exit(0 if importlib.util.find_spec(sys.argv[1]) else 1)' websocket
if ($LASTEXITCODE -ne 0) {
  Write-Host 'Installing websocket-client into the private environment...'
  & $python -m pip install websocket-client
  if ($LASTEXITCODE -ne 0) { throw 'Could not install websocket-client. Check this PC has internet access.' }
}
& $python -c 'import importlib.util,sys; sys.exit(0 if importlib.util.find_spec(sys.argv[1]) else 1)' ytmusicapi
if ($LASTEXITCODE -ne 0) {
  & $python -m pip install 'ytmusicapi>=1.8,<2'
  if ($LASTEXITCODE -ne 0) { throw 'Could not install ytmusicapi for song search.' }
}
& $python -c 'import importlib.util,sys; sys.exit(0 if importlib.util.find_spec(sys.argv[1]) else 1)' pycaw
if ($LASTEXITCODE -ne 0) {
  & $python -m pip install pycaw
  if ($LASTEXITCODE -ne 0) { Write-Warning 'Audio ducking requires pycaw. Chat and music controls can still run.' }
}
& $python -c 'import importlib.util,sys; sys.exit(0 if importlib.util.find_spec(sys.argv[1]) else 1)' PIL
if ($LASTEXITCODE -ne 0) {
  Write-Host 'Installing Pillow for the artwork fallback...'
  & $python -m pip install 'Pillow>=11'
  if ($LASTEXITCODE -ne 0) { Write-Warning 'Pillow is unavailable. Direct artwork and other Pod features can still run.' }
}
$bridge = Join-Path $PSScriptRoot 'assistant_bridge.py'
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) { throw 'Keep assistant_bridge.py beside this script.' }
Write-Host "Starting Pod AI bridge on port $Port. Leave this window open while using Chat; press Ctrl+C to stop."
Write-Host 'Pod Settings will open in your browser. Add or update your API key there.'
Write-Host 'The assistant voice is AI-generated.'
& $python $bridge --token-file $tokenPath --port $Port
if ($LASTEXITCODE -ne 0) { throw 'AI bridge stopped. See the message above.' }
