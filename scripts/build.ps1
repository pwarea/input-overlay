param([string]$Zig = 'zig', [switch]$Test, [string]$OutputPath = 'dist/Input Overlay.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force build,dist | Out-Null
    $sourcePrefix = $projectRoot.Replace('\', '/')
    $buildCommit = ''
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $revision = & git rev-parse --verify HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $revision -match '^[0-9a-f]{40}$') { $buildCommit = $revision }
    }
    $versionMatch = [regex]::Match((Get-Content -LiteralPath CMakeLists.txt -Raw), '\bproject\s*\(\s*\w+\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
    if (!$versionMatch.Success) { throw 'Project version was not found.' }
    $common = @('-target', 'x86_64-windows-gnu', '-mcpu=baseline', '-std=c++17', '-O2', '-g0', '-fno-ident', "-ffile-prefix-map=$projectRoot=.", "-ffile-prefix-map=$sourcePrefix=.", '-DUNICODE', '-D_UNICODE', '-DNOMINMAX', '-D_WIN32_WINNT=0x0a00', '-Isrc')
    $common += '-DINPUT_OVERLAY_BUILD_COMMIT="' + $buildCommit + '"'
    $common += '-DINPUT_OVERLAY_VERSION="' + $versionMatch.Groups[1].Value + '"'
    & $Zig rc /Isrc /fo build/app.res src/app.rc
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & $Zig c++ @common -Wall -Wextra -municode '-Wl,/subsystem:windows' '-Wl,--build-id=none' -s -static src/main.cpp src/config.cpp src/settings.cpp src/color_picker.cpp src/overlay.cpp src/injected_input.cpp src/controller.cpp src/updates.cpp src/update_install.cpp build/app.res -luser32 -lgdi32 -lgdiplus -lcomctl32 -lshell32 -ladvapi32 -ldwmapi -lwtsapi32 -luxtheme -lwinhttp -lbcrypt -o $OutputPath
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
    if ($Test) {
        & $Zig c++ @common tests/core_tests.cpp src/config.cpp -luser32 -ladvapi32 -ldwmapi -o build/core_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        & './build/core_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
        & $Zig c++ @common tests/color_picker_tests.cpp -luser32 -lgdi32 -ldwmapi -o build/color_picker_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Color picker test compilation failed.' }
        & './build/color_picker_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Color picker tests failed.' }
        & $Zig c++ @common tests/overlay_smoke.cpp src/overlay.cpp -luser32 -lgdi32 -lgdiplus -o build/overlay_smoke.exe
        if ($LASTEXITCODE -ne 0) { throw 'Renderer test compilation failed.' }
        & './build/overlay_smoke.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Renderer tests failed.' }
        & $Zig c++ @common tests/application_visibility_tests.cpp src/config.cpp src/color_picker.cpp src/overlay.cpp src/injected_input.cpp src/controller.cpp src/updates.cpp src/update_install.cpp -luser32 -lgdi32 -lgdiplus -lcomctl32 -lshell32 -ladvapi32 -ldwmapi -lwtsapi32 -luxtheme -lwinhttp -lbcrypt -o build/application_visibility_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Application visibility test compilation failed.' }
        & './build/application_visibility_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Application visibility tests failed.' }
        & $Zig c++ @common tests/injected_input_tests.cpp src/injected_input.cpp -luser32 -o build/injected_input_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Input worker test compilation failed.' }
        & './build/injected_input_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Input worker tests failed.' }
        & $Zig c++ @common tests/controller_tests.cpp src/controller.cpp -o build/controller_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Controller test compilation failed.' }
        & './build/controller_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Controller tests failed.' }
        & $Zig c++ @common tests/update_tests.cpp src/updates.cpp -lwinhttp -lbcrypt -o build/update_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Update test compilation failed.' }
        & './build/update_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Update tests failed.' }
        & $Zig c++ @common tests/update_install_tests.cpp src/update_install.cpp -lbcrypt -lshell32 -o build/update_install_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Installer test compilation failed.' }
        & './build/update_install_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Installer tests failed.' }
    }
    Write-Output $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
} finally { Pop-Location }
