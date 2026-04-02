$Content = Get-Content -Path "src/renderer/SoftwareRenderer.cpp"
$NewContent = New-Object System.Collections.Generic.List[string]
$Skip = $false
$BraceCount = 0
$FunctionsToRemove = @(
    "void SoftwareRenderer::ApplyDepthOfField(",
    "void SoftwareRenderer::ApplyDenoising(",
    "void SoftwareRenderer::ApplyPostProcess(",
    "Vec3 ColorTemperatureToRgb(",
    "double EvaluateCustomCurve(",
    "Vec3 ApplyLevelsCurve(",
    "Vec3 ACESFilm("
)

foreach ($Line in $Content) {
    if (!$Skip) {
        $Found = $false
        foreach ($Func in $FunctionsToRemove) {
            if ($Line.Contains($Func)) {
                $Found = $true
                break
            }
        }
        if ($Found) {
            $Skip = $true
            $BraceCount = 0
            if ($Line.Contains("{")) {
                $BraceCount += ($Line.ToCharArray() | Where-Object { $_ -eq "{" }).Count
            }
            if ($Line.Contains("}")) {
                $BraceCount -= ($Line.ToCharArray() | Where-Object { $_ -eq "}" }).Count
            }
            if ($Skip -and $BraceCount -eq 0 -and $Line.Contains("{")) {
                # Just started, let it proceed to next lines to find closing }
            } elseif ($Skip -and $BraceCount -le 0 -and $Line.Contains("}")) {
                 # Single line function or similar
                 $Skip = $false
            }
            continue;
        }
        $NewContent.Add($Line)
    } else {
        # Currently skipping
        $BraceCount += ($Line.ToCharArray() | Where-Object { $_ -eq "{" }).Count
        $BraceCount -= ($Line.ToCharArray() | Where-Object { $_ -eq "}" }).Count
        
        if ($BraceCount -le 0 -and $Line.Contains("}")) {
            $Skip = $false
        }
    }
}

$NewContent | Set-Content -Path "src/renderer/SoftwareRenderer.cpp"
