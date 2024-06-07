$MSBuildExe = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$MSBuildCmdLineArgs = @('LowResolutionMHW.sln', '/property:Configuration=Release;Platform=x64', '/target:Rebuild', '/maxCpuCount')

New-Item -Force -ItemType Directory -Name 'CollectedDLLs'

$TargetHeights = @('58', '104', '144', '160', '192', '200', '224', '240', '256', '320', '360', '448', '480', '500', '512', '576', '600', '640', '720', '768', '800', '864', '900', '1000', '1024', '1050', '1080', '1152', '1200', '1440', '1536', '1660', '2160')
foreach ($Height in $TargetHeights)
{
    $env:CL = "/DLRR_TARGET_HEIGHT#${Height}"
    & $MSBuildExe $MSBuildCmdLineArgs "/property:TargetName=LowerRenderResolution_${Height}p" "/property:OutputType=Library"
    Copy-Item -LiteralPath "x64\Release\LowerRenderResolution_${Height}p.dll" -Destination 'CollectedDLLs'
}

# Informing the user that the script execution is complete
Write-Host "Build script execution complete!"
