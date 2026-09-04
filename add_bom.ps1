$files = Get-ChildItem -Recurse -Include *.h,*.cpp,*.hpp,*.inl src/
foreach ($f in $files) {
    $content = [System.IO.File]::ReadAllText($f.FullName)
    $bom = [System.Text.Encoding]::UTF8.GetPreamble()
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($content)
    $full = $bom + $bytes
    [System.IO.File]::WriteAllBytes($f.FullName, $full)
    Write-Host "Added BOM to $($f.FullName)"
}