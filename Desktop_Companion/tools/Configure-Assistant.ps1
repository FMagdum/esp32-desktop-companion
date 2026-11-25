param([string]$CardRoot, [string]$DesktopIP, [int]$Port = 9870)
$ErrorActionPreference = 'Stop'
if ($Port -lt 1 -or $Port -gt 65535) { throw 'Port must be between 1 and 65535.' }
if (-not $CardRoot) { $CardRoot = Read-Host 'Enter the microSD drive letter (for example E:)' }
if ($CardRoot -match '^[A-Za-z]:$') { $CardRoot += '\' }
$card = (Resolve-Path -LiteralPath $CardRoot).ProviderPath
$driveRoot = [IO.Path]::GetPathRoot($card)
if ($card.TrimEnd('\') -ine $driveRoot.TrimEnd('\')) { throw 'Select the root of the microSD drive.' }
if (-not (Test-Path -LiteralPath (Join-Path $card 'companion.json') -PathType Leaf)) {
  throw 'No companion.json found at this drive root. Select the same microSD used for YTMDesktop.'
}
if (-not $DesktopIP) {
  Write-Host 'Available IPv4 addresses on this PC:'
  Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
    Select-Object InterfaceAlias, IPAddress | Format-Table | Out-Host
  $DesktopIP = Read-Host 'Enter the IPv4 address of this PC on the same Wi-Fi network as the board'
}
$ip = $null
if (-not [Net.IPAddress]::TryParse($DesktopIP, [ref]$ip) -or
    $ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or
    [Net.IPAddress]::IsLoopback($ip) -or $DesktopIP -eq '0.0.0.0' -or $ip.GetAddressBytes()[0] -ge 224) {
  throw 'Enter a valid, non-loopback IPv4 address for this PC.'
}
$target = Join-Path $card 'assistant.json'
if (Test-Path -LiteralPath $target) {
  $answer = Read-Host 'assistant.json already exists on this card. Replace it with the selected bridge settings? [y/N]'
  if ($answer -notmatch '^[Yy]$') { Write-Host 'Existing configuration retained.'; exit 0 }
}
$tokenDir = Join-Path $env:LOCALAPPDATA 'DesktopCompanion'
$tokenPath = Join-Path $tokenDir 'assistant-token.txt'
New-Item -ItemType Directory -Force -Path $tokenDir | Out-Null
if (Test-Path -LiteralPath $tokenPath -PathType Leaf) {
  $token = ([IO.File]::ReadAllText($tokenPath)).Trim()
  if ($token -notmatch '^[0-9a-f]{64}$') { throw 'Invalid existing PC bridge token; fix it before continuing.' }
} else {
  $bytes = New-Object byte[] 32
  $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
  try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
  $token = [BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
  [IO.File]::WriteAllText($tokenPath, $token, (New-Object Text.UTF8Encoding($false)))
}
$json = [ordered]@{ host=$ip.ToString(); port=$Port; token=$token } | ConvertTo-Json -Compress
[IO.File]::WriteAllText($target, $json, (New-Object Text.UTF8Encoding($false)))
Write-Host 'Assistant configuration saved to the same microSD. Existing sounds and music settings are untouched.'
Write-Host 'Reinsert the card and restart the board. Keep the PC token file private.'
