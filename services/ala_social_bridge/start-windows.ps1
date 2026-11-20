# Stage 5.2 experimental. Plain HTTP is ONLY for an explicitly trusted LAN.
param(
    [switch]$AcceptTrustedLanHttp,
    [string]$BindAddress = "0.0.0.0",
    [int]$Port = 8765
)
$ErrorActionPreference = "Stop"
if (-not $AcceptTrustedLanHttp) {
    throw "HTTP+bearer NIE szyfruje nagrań. Uruchom ponownie z -AcceptTrustedLanHttp wyłącznie w zaufanym LAN."
}
if ($Port -lt 1024 -or $Port -gt 65535) { throw "Port musi mieć wartość 1024-65535." }
if (-not $env:OPENAI_API_KEY) {
    $secret = Read-Host "OPENAI_API_KEY (nie bedzie wyswietlany)" -AsSecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
    try { $env:OPENAI_API_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
}
if (-not $env:ALA_BRIDGE_TOKEN) {
    $bytes = New-Object byte[] 32
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    $env:ALA_BRIDGE_TOKEN = [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+','-').Replace('/','_')
    Write-Host "Skopiuj TOKEN do panelu Ali (nie udostepniaj go): $env:ALA_BRIDGE_TOKEN"
}
Push-Location $PSScriptRoot
try {
    Write-Host "TRYB EKSPERYMENTALNY: bez TLS, tylko lokalny zaufany LAN; Ctrl+C konczy."
    py -3 server.py --host $BindAddress --port $Port
} finally {
    Pop-Location
    Remove-Item Env:OPENAI_API_KEY -ErrorAction SilentlyContinue
}
