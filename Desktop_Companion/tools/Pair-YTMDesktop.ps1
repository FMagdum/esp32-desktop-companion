param([string]$CardRoot, [string]$DesktopIP, [int]$Port = 9863)
$ErrorActionPreference = 'Stop'

$script:pairingStage = 'initialization'
$script:pairingFailure = ''

function Invoke-YtmRequest {
  param([hashtable]$Arguments)
  try { return Invoke-RestMethod @Arguments }
  catch {
    $httpStatus = $null
    try { $httpStatus = [int]$_.Exception.Response.StatusCode } catch {}
    if ($httpStatus) {
      $script:pairingFailure = "HTTP $httpStatus."
      switch ($httpStatus) {
        401 { $script:pairingFailure += ' Authorization was rejected.' }
        403 { $script:pairingFailure += ' Check companion authorization is enabled and approve the matching code.' }
        404 { $script:pairingFailure += ' Check that this is YTMDesktop 2.1.0 with Companion Server enabled.' }
        429 { $script:pairingFailure += ' Too many requests; wait at least 60 seconds before retrying.' }
        default { $script:pairingFailure += ' The desktop server returned an error.' }
      }
    } else {
      $script:pairingFailure = 'No HTTP response (' + $_.Exception.GetType().Name + '). '
      if ($script:pairingStage -eq 'requesting approval') {
        $script:pairingFailure += 'Approve the matching code in YTMDesktop within 30 seconds.'
      } else {
        $script:pairingFailure += 'Check YTMDesktop is running on this PC and Companion Server uses the selected port.'
      }
    }
    # Never include response bodies, request headers or exception payloads.
    throw 'YTMDesktop request failed.'
  }
}

function Get-YtmToken {
  param([string]$BaseUrl)
  $script:pairingStage = 'checking /metadata'
  Write-Host "Checking Companion Server at $BaseUrl ..."
  $metadata = Invoke-YtmRequest @{ Uri="$BaseUrl/metadata"; Method='Get'; TimeoutSec=5 }
  if ('v1' -notin @($metadata.apiVersions)) {
    $script:pairingFailure = 'The desktop does not expose Companion API v1. Check the app/version.'
    throw 'Unsupported Companion API.'
  }
  $identity = @{ appId='desktopcompanionesp32'; appName='Desktop Companion ESP32'; appVersion='0.6.0' }
  $script:pairingStage = 'requesting pairing code'
  $response = Invoke-YtmRequest @{ Uri="$BaseUrl/api/v1/auth/requestcode"; Method='Post'; ContentType='application/json'; Body=($identity | ConvertTo-Json -Compress); TimeoutSec=5 }
  if (-not $response.code) {
    $script:pairingFailure = 'YTMDesktop returned no pairing code.'
    throw 'Missing pairing code.'
  }
  Write-Host "Pairing code: $($response.code)"
  Write-Host 'Approve Desktop Companion ESP32 in YTMDesktop now (within 30 seconds).'
  $body = @{ appId=$identity.appId; code=[string]$response.code }
  $script:pairingStage = 'requesting approval'
  $auth = Invoke-YtmRequest @{ Uri="$BaseUrl/api/v1/auth/request"; Method='Post'; ContentType='application/json'; Body=($body | ConvertTo-Json -Compress); TimeoutSec=35 }
  if (-not $auth.token -or $auth.token.Length -gt 512 -or $auth.token -match '[^!-~]') { $script:pairingFailure='YTMDesktop returned an invalid token.'; throw 'Invalid token.' }
  $script:pairingStage = 'validating authorization'
  $null = Invoke-YtmRequest @{ Uri="$BaseUrl/api/v1/state"; Method='Get'; Headers=@{Authorization=[string]$auth.token}; TimeoutSec=5 }
  return [string]$auth.token
}
if ($MyInvocation.InvocationName -eq '.') { return }
if (-not $CardRoot) { $CardRoot = Read-Host 'microSD drive root (for example E:\)' }
$root = [IO.Path]::GetFullPath($CardRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container) -or $root.TrimEnd('\') -ne [IO.Path]::GetPathRoot($root).TrimEnd('\') -or [IO.Path]::GetPathRoot($root) -eq [IO.Path]::GetPathRoot($env:SystemRoot)) {
  throw 'Select the mounted microSD drive root, not a project folder or system drive.'
}
if (-not $DesktopIP) { $DesktopIP = Read-Host 'Desktop LAN IPv4 address (not localhost)' }
$address = $null
if (-not [Net.IPAddress]::TryParse($DesktopIP, [ref]$address) -or $address.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or $address.GetAddressBytes()[0] -eq 127 -or $address.GetAddressBytes()[0] -ge 224 -or $DesktopIP -eq '0.0.0.0' -or $Port -lt 1 -or $Port -gt 65535) {
  throw 'Enter a valid LAN IPv4 address and port.'
}
$ssid = Read-Host '2.4 GHz Wi-Fi name (SSID)'
if ([Text.Encoding]::UTF8.GetByteCount($ssid) -lt 1 -or [Text.Encoding]::UTF8.GetByteCount($ssid) -gt 32) { throw 'SSID must be 1-32 UTF-8 bytes.' }
$securePassword = Read-Host 'Wi-Fi password (hidden)' -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
try { $wifiPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer) }
finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer) }
if ([Text.Encoding]::UTF8.GetByteCount($wifiPassword) -gt 64) { throw 'Wi-Fi password is too long.' }
$destination = Join-Path $root 'companion.json'
if (Test-Path -LiteralPath $destination) {
  $answer = Read-Host 'Replace the existing companion.json on this card? Type YES'
  if ($answer -ne 'YES') { throw 'Existing configuration kept.' }
}
try {
  $authToken = Get-YtmToken -BaseUrl "http://127.0.0.1:$Port"
  $configuration = @{ssid=$ssid; password=$wifiPassword; host=$DesktopIP; port=$Port; token=$authToken}
  $script:pairingStage = 'writing companion.json to microSD'
  $script:pairingFailure = 'Check that the card is writable, has free space and its drive letter is correct.'
  [IO.File]::WriteAllText($destination, ($configuration | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
  Write-Host 'Configuration saved on microSD. Eject it, power off the board, insert and reboot.'
} catch {
  if (-not $script:pairingFailure) { $script:pairingFailure='Check the selected port, desktop approval and card access.' }
  throw "Pairing/setup failed at '$script:pairingStage'. $script:pairingFailure"

} finally {
  $wifiPassword=$null
  $authToken=$null
  $configuration=$null
}

