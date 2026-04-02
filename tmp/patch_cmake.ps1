$Content = Get-Content -Path "CMakeLists.txt"
$NewContent = New-Object System.Collections.Generic.List[string]
foreach ($Line in $Content) {
    $NewContent.Add($Line)
    if ($Line -match "src/renderer/SoftwareRenderer.cpp") {
        # Check if already has Effects.cpp
        # Actually just add it after every SoftwareRenderer.cpp unless it's already there
        $NextIndex = $NewContent.Count
        if ($NextIndex -lt $Content.Count -and $Content[$NextIndex] -notmatch "src/renderer/Effects.cpp") {
             $NewContent.Add("    src/renderer/Effects.cpp")
        }
    }
}
$NewContent | Set-Content -Path "CMakeLists.txt"
