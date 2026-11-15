param(
    [string]$QtDir = 'C:\Qt\5.15.2\msvc2019_64',
    [string]$MsvcRoot = $env:MSVC_ROOT,
    [string]$WindowsSdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$WindowsSdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
if (-not $MsvcRoot) {
    $MsvcRoot = 'C:\Toolchains\MSVC-14.44.35207\VC\Tools\MSVC\14.44.35207'
}
$root = Split-Path $PSScriptRoot -Parent
$suites = @(
    @{ Name = 'volume-dialog'; Source = 'volume_dialog_test' },
    @{ Name = 'server-events'; Source = 'server_events_test' },
    @{ Name = 'self-state'; Source = 'self_state_test' }
)
$compiler = Join-Path $MsvcRoot 'bin\Hostx64\x64\cl.exe'
$moc = Join-Path $QtDir 'bin\moc.exe'
foreach ($required in @($compiler, $moc, "$QtDir\lib\Qt5Test.lib", "$QtDir\plugins\platforms\qoffscreen.dll", "$WindowsSdkRoot\Lib\$WindowsSdkVersion\um\x64\kernel32.lib")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing prerequisite: $required" }
}
$failures = @()
$names = @('PATH', 'INCLUDE', 'LIB', 'QT_QPA_PLATFORM', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_FONTDIR')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:PATH = "$MsvcRoot\bin\Hostx64\x64;$QtDir\bin;$WindowsSdkRoot\bin\$WindowsSdkVersion\x64;$env:PATH"
    $env:INCLUDE = "$MsvcRoot\include;$WindowsSdkRoot\Include\$WindowsSdkVersion\ucrt;$WindowsSdkRoot\Include\$WindowsSdkVersion\shared;$WindowsSdkRoot\Include\$WindowsSdkVersion\um;$WindowsSdkRoot\Include\$WindowsSdkVersion\winrt"
    $env:LIB = "$MsvcRoot\lib\x64;$WindowsSdkRoot\Lib\$WindowsSdkVersion\ucrt\x64;$WindowsSdkRoot\Lib\$WindowsSdkVersion\um\x64"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtDir\plugins\platforms"
    $env:QT_PLUGIN_PATH = "$QtDir\plugins"
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
    foreach ($suite in $suites) {
        $out = Join-Path $root "build\tests\$($suite.Name)"
        New-Item -ItemType Directory -Force -Path $out | Out-Null
        $source = Join-Path $root "tests\$($suite.Source).cpp"
        try {
            & $moc $source -o "$out\$($suite.Source).moc"
            if ($LASTEXITCODE -ne 0) { throw "moc failed with exit code $LASTEXITCODE" }
            $exe = Join-Path $out "$($suite.Source).exe"
            $arguments = @('/nologo', '/EHsc', '/O2', '/std:c++17', '/MD', '/W3', '/utf-8', '/DQT_WIDGETS_LIB', '/DQT_GUI_LIB',
                "/I$QtDir\include", "/I$QtDir\include\QtCore", "/I$QtDir\include\QtGui",
                "/I$QtDir\include\QtWidgets", "/I$QtDir\include\QtTest", "/I$out",
                "/I$root\sdk\include", "/I$root\sdk\include\teamspeak", "/Fo$out\",
                $source, "$root\src\channelpanel.cpp", "/Fe$exe", '/link', "/LIBPATH:$QtDir\lib",
                'Qt5Test.lib', 'Qt5Widgets.lib', 'Qt5Gui.lib', 'Qt5Core.lib', 'user32.lib', 'shell32.lib')
            & $compiler @arguments
            if ($LASTEXITCODE -ne 0) { throw "Compilation failed with exit code $LASTEXITCODE" }
            & $exe '-platform' 'offscreen' '-o' '-,txt' '-o' "$out\results.xml,xml"
            if ($LASTEXITCODE -ne 0) { throw "Regression tests failed with exit code $LASTEXITCODE" }
            Write-Output "[ok] $($suite.Name) offscreen regression tests passed. Results: $out\results.xml"
        } catch {
            $failures += "$($suite.Name): $($_.Exception.Message)"
            Write-Warning $failures[-1]
        }
    }
    if ($failures.Count) { throw ($failures -join [Environment]::NewLine) }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
