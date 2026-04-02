$Content = Get-Content -Path "src/renderer/SoftwareRenderer.cpp"
$NewContent = New-Object System.Collections.Generic.List[string]
$Skip = $false
$FunctionsToRemove = @(
    "void SoftwareRenderer::ApplyDepthOfField(",
    "void SoftwareRenderer::ApplyDenoising(",
    "void SoftwareRenderer::ApplyPostProcess(",
    "Vec3 ColorTemperatureToRgb(",
    "double EvaluateCustomCurve(",
    "Vec3 ApplyLevelsCurve("
)

foreach ($Line in $Content) {
    if (!$Skip) {
        foreach ($Func in $FunctionsToRemove) {
            if ($Line.Contains($Func)) {
                $Skip = $true
                break
            }
        }
    }

    if (!$Skip) {
        $NewContent.Add($Line)
    } elseif ($Line.Trim() -eq "}") {
        $Skip = $false
    }
}

$NewContent | Set-Content -Path "src/renderer/SoftwareRenderer.cpp"
