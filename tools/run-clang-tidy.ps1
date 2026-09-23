#requires -Version 5.1
# run-clang-tidy.ps1 - Progressive clang-tidy WarningsAsErrors gate.
#
# Runs clang-tidy against a target header file, treating ONLY warnings in that
# file as errors (exit != 0). Warnings in the translation unit, test framework,
# or third-party code are silently ignored via --line-filter.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1
#
# Exit codes:
#   0 - target module has zero clang-tidy warnings (gate passes)
#   1 - target module has warnings, or clang-tidy failed to run
#
# To add a module: append { File = "<relative path>", Tu = "<driver .cpp>" }
# to the $TargetModules table below.

$ProjectRoot = Split-Path $PSScriptRoot -Parent
$BuildDir    = Join-Path $ProjectRoot "build"
$OutDir      = Join-Path $BuildDir "bin/Release/out"
$OutFile     = Join-Path $OutDir "clang_tidy_step6.txt"

# VS2022 ships LLVM 19.1.5; point VCToolsInstallDir at the compatible MSVC STL
# (14.44) to avoid the VS18 STL requiring Clang 20+ version check.
$ClangTidy = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
$env:VCToolsInstallDir = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\"

# Progressive gate table: File = gated header; Tu = driver translation unit.
$TargetModules = @(
    [PSCustomObject]@{ File = "src/render/LodGroup.h";           Tu = "src/tests/test_lod_group.cpp" }
    [PSCustomObject]@{ File = "src/core/FrameProfiler.h";        Tu = "src/tests/test_core.cpp" }
    [PSCustomObject]@{ File = "src/render/OcclusionCulling.h";    Tu = "src/tests/test_occlusion_culling.cpp" }
    [PSCustomObject]@{ File = "src/render/LightProbe.h";          Tu = "src/tests/test_light_probe.cpp" }
    [PSCustomObject]@{ File = "src/navigation/NavMesh.h";          Tu = "src/tests/test_navmesh.cpp" }
    [PSCustomObject]@{ File = "src/render/descriptor_set.h";       Tu = "src/tests/test_render_logic.cpp" }
    [PSCustomObject]@{ File = "src/scene/EcsScene.h";              Tu = "src/tests/test_ecs_scene.cpp" }
    [PSCustomObject]@{ File = "src/scene/Transform.h";            Tu = "src/tests/test_transform_cache.cpp" }
    [PSCustomObject]@{ File = "src/ui/UiModel.h";                 Tu = "src/tests/test_ui.cpp" }
    [PSCustomObject]@{ File = "src/render/Renderer.h";            Tu = "src/render/Renderer.cpp" }
    [PSCustomObject]@{ File = "src/render/pipeline.h";              Tu = "src/render/Renderer.cpp" }
    [PSCustomObject]@{ File = "src/render/PostProcessor.h";        Tu = "src/render/PostProcessor.cpp" }
    [PSCustomObject]@{ File = "src/render/FrameStaging.h";         Tu = "src/render/FrameStaging.cpp" }
    [PSCustomObject]@{ File = "src/render/MemoryPools.h";          Tu = "src/render/MemoryPools.cpp" }
    [PSCustomObject]@{ File = "src/render/TransientAllocator.h";   Tu = "src/render/TransientAllocator.cpp" }
    [PSCustomObject]@{ File = "src/render/TransientMemoryPool.h";  Tu = "src/tests/test_render_logic.cpp" }
    [PSCustomObject]@{ File = "src/render/ParallelCommandRecorder.h"; Tu = "src/render/ParallelCommandRecorder.cpp" }
    [PSCustomObject]@{ File = "src/render/ubo_structs.h";         Tu = "src/tests/test_render_logic.cpp" }
    [PSCustomObject]@{ File = "src/app/systems/ParticleHost.h";     Tu = "src/app/systems/ParticleHost.cpp" }
    [PSCustomObject]@{ File = "src/app/systems/PhysicsHost.h";     Tu = "src/app/systems/PhysicsHost.cpp" }
    [PSCustomObject]@{ File = "src/audio/AudioEngine.h";          Tu = "src/tests/test_audio.cpp" }
    [PSCustomObject]@{ File = "src/audio/AudioMixer.h";           Tu = "src/tests/test_audio.cpp" }
    [PSCustomObject]@{ File = "src/audio/Sound.h";                 Tu = "src/tests/test_audio.cpp" }
    [PSCustomObject]@{ File = "src/audio/SoundSource.h";           Tu = "src/tests/test_audio.cpp" }
    [PSCustomObject]@{ File = "src/app/systems/NavHost.h";         Tu = "src/tests/test_navmesh_wiring.cpp" }
    [PSCustomObject]@{ File = "src/app/systems/AnimationHost.h";   Tu = "src/app/systems/AnimationHost.cpp" }
    [PSCustomObject]@{ File = "src/app/systems/PostProcessSync.h"; Tu = "src/app/systems/PostProcessSync.cpp" }
    [PSCustomObject]@{ File = "src/app/systems/SceneIoHost.h";     Tu = "src/app/systems/SceneIoHost.cpp" }
    [PSCustomObject]@{ File = "src/app/systems/ShowcaseHost.h";   Tu = "src/tests/test_showcase_host.cpp" }
    [PSCustomObject]@{ File = "src/scene/AvatarRetarget.h";        Tu = "src/tests/test_avatar_retarget.cpp" }
    [PSCustomObject]@{ File = "src/scene/PersonHost.h";           Tu = "src/tests/test_person.cpp" }
    [PSCustomObject]@{ File = "src/scene/Camera.h";                Tu = "src/tests/test_scene.cpp" }
    [PSCustomObject]@{ File = "src/scene/FirstPersonCamera.h";     Tu = "src/scene/FirstPersonCamera.cpp" }
    [PSCustomObject]@{ File = "src/script/CSharpHost.h";           Tu = "src/tests/test_script.cpp" }
    [PSCustomObject]@{ File = "src/script/ScriptFields.h";         Tu = "src/tests/test_inspector.cpp" }
    [PSCustomObject]@{ File = "samples/open_world/OpenWorldScene.h"; Tu = "src/tests/test_openworld.cpp" }
    [PSCustomObject]@{ File = "samples/showcase/CyberCity.h";       Tu = "src/tests/test_cyber_city.cpp" }
    [PSCustomObject]@{ File = "samples/vertical_slice/SliceScene.h"; Tu = "src/tests/test_slice.cpp" }
    [PSCustomObject]@{ File = "samples/voxel/VoxelWorld.h";        Tu = "src/tests/test_voxel_world.cpp" }
    [PSCustomObject]@{ File = "src/app/Application.h";           Tu = "src/app/Application.cpp" }
    [PSCustomObject]@{ File = "src/editor/EditorOverlay.h";       Tu = "src/editor/EditorOverlay.cpp" }
)

if (-not (Test-Path $ClangTidy)) {
    Write-Host "clang-tidy not found: $ClangTidy" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path (Join-Path $BuildDir "compile_commands.json"))) {
    Write-Host "compile_commands.json missing in $BuildDir" -ForegroundColor Red
    exit 1
}

$null = New-Item -ItemType Directory -Path $OutDir -Force
$allOutput = New-Object System.Text.StringBuilder
$hasWarning = $false

foreach ($mod in $TargetModules) {
    $absTu = Join-Path $ProjectRoot $mod.Tu
    # Use \" for embedded quotes (PowerShell 5.1 native arg passing convention).
    $lineFilter = "--line-filter=[{\`"name\`":\`"$($mod.File)\`",\`"lines\`":[[1,999999]]}]"

    Write-Host "==> clang-tidy gate: $($mod.File) (via $($mod.Tu))"
    # Merge stderr into stdout; do not let native stderr throw.
    $section = & $ClangTidy -p="$BuildDir" $absTu $lineFilter -warnings-as-errors="*" 2>&1 | Out-String

    [void]$allOutput.AppendLine("=== $($mod.File) ===")
    [void]$allOutput.AppendLine($section)

    # Count only real diagnostic lines (path:line:col: warning/error),
    # not summary lines like "42728 warnings generated." or "Suppressed ...".
    $diagLines = [regex]::Matches($section, '(?m)^[A-Za-z]:[\\/].*?:\d+:\d+: (warning|error):')
    if ($diagLines.Count -gt 0) { $hasWarning = $true }
}

[System.IO.File]::WriteAllText($OutFile, $allOutput.ToString(), (New-Object System.Text.UTF8Encoding($false)))

if ($hasWarning) {
    Write-Host "clang-tidy gate FAILED - diagnostics found in target. See $OutFile" -ForegroundColor Red
    exit 1
}
Write-Host "clang-tidy gate PASSED - 0 diagnostics in target modules. Evidence: $OutFile" -ForegroundColor Green
exit 0
