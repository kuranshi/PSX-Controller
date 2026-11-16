$ErrorActionPreference = 'Stop'

$projectRoot = $PSScriptRoot
$appVersion = '0.1.0'
$venvPython = Join-Path $projectRoot '.venv-build\Scripts\python.exe'
$driverFolder = Join-Path $projectRoot 'installer\prerequisites'
$driverName = 'ViGEmBus_1.22.0_x64_x86_arm64.exe'
$driverPath = Join-Path $driverFolder $driverName
$driverUrl = 'https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe'
$expectedDriverHash = '89220A7865076B342892F98865F3499FB7C4CFD673159E89D352C360FD014C6A'

if (-not (Test-Path $venvPython)) {
    python -m venv (Join-Path $projectRoot '.venv-build')
}
if (-not (Test-Path $venvPython)) {
    throw 'Could not create the local Python build environment.'
}

$env:VGAMEPAD_SKIP_VIGEMBUS_INSTALL = 'true'
$pipAvailable = $false
& $venvPython -m pip --version *> $null
$pipAvailable = ($LASTEXITCODE -eq 0)
if ($pipAvailable) {
    & $venvPython -m pip install --disable-pip-version-check -r (Join-Path $projectRoot 'requirements-build.txt')
} else {
    python -m pip --python $venvPython install --disable-pip-version-check -r (Join-Path $projectRoot 'requirements-build.txt')
}
if ($LASTEXITCODE -ne 0) {
    throw 'Installing Python build requirements failed.'
}

$licenseFolder = Join-Path $projectRoot 'licenses'
New-Item -ItemType Directory -Path $licenseFolder -Force | Out-Null
$sitePackages = Join-Path $projectRoot '.venv-build\Lib\site-packages'
$pyinstallerLicense = Join-Path $sitePackages 'PyInstaller\COPYING.txt'
if (-not (Test-Path $pyinstallerLicense)) {
    $pyinstallerLicense = Join-Path $sitePackages 'pyinstaller-6.22.3.dist-info\licenses\COPYING.txt'
}
if (-not (Test-Path $pyinstallerLicense)) {
    throw 'PyInstaller license text was not found in the build environment.'
}
Copy-Item -LiteralPath $pyinstallerLicense -Destination (Join-Path $licenseFolder 'pyinstaller-COPYING.txt') -Force

New-Item -ItemType Directory -Path $driverFolder -Force | Out-Null
$validCachedDriver = $false
if (Test-Path $driverPath) {
    $cachedHash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
    $cachedSignature = Get-AuthenticodeSignature -LiteralPath $driverPath
    $validCachedDriver = ($cachedHash -eq $expectedDriverHash -and $cachedSignature.Status -eq 'Valid')
}
if (-not $validCachedDriver) {
    Invoke-WebRequest -Uri $driverUrl -OutFile $driverPath
}

$driverHash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
$driverSignature = Get-AuthenticodeSignature -LiteralPath $driverPath
if ($driverHash -ne $expectedDriverHash) {
    throw "ViGEmBus installer SHA-256 mismatch: $driverHash"
}
if ($driverSignature.Status -ne 'Valid' -or $driverSignature.SignerCertificate.Subject -notmatch 'Nefarius Software Solutions') {
    throw 'The ViGEmBus installer signature is invalid or has an unexpected publisher.'
}

$iscc = (Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($candidate in @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        'C:\Program Files (x86)\Inno Setup 7\ISCC.exe',
        'C:\Program Files\Inno Setup 7\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe'
    )) {
        if (Test-Path $candidate) {
            $iscc = $candidate
            break
        }
    }
}
if (-not $iscc) {
    throw 'Inno Setup Compiler (ISCC.exe) is required. Install Inno Setup 7, then run this script again.'
}

Push-Location $projectRoot
try {
    & $venvPython -m PyInstaller --noconfirm --clean --windowed --onedir `
        --name 'PSXController' `
        --distpath (Join-Path $projectRoot 'dist\app') `
        --workpath (Join-Path $projectRoot 'build\pyinstaller') `
        --specpath (Join-Path $projectRoot 'build') `
        --collect-all vgamepad `
        --hidden-import serial.tools.list_ports_windows `
        'controller_app.py'
    if ($LASTEXITCODE -ne 0) {
        throw 'PyInstaller failed to bundle the Windows application.'
    }

    & $iscc "/DAppVersion=$appVersion" (Join-Path $projectRoot 'installer\PSXController.iss')
    if ($LASTEXITCODE -ne 0) {
        throw 'Inno Setup failed to create the installer.'
    }
}
finally {
    Pop-Location
}

Write-Host "Installer created under: $(Join-Path $projectRoot 'dist\installer')"

# by codex
