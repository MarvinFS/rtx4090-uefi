. (Join-Path $PSScriptRoot '..\..\package\Gpu4090Common.ps1')
# Pester 3.4 (inbox on Windows): powershell -NoProfile -Command "Invoke-Pester -Path win4090/tests/windows -EnableExit"
# Every case uses 85 MB needed and 128 MB for a new partition.

Describe 'Select-Gpu4090Target' {
  It 'reuses a big enough GPU4090 partition first' {
    Select-Gpu4090Target 'Auto' 124 500 1000 85 128 'GPT' | Should Be 'Reuse' }
  It 'falls back to the ESP when there is no GPU4090 partition' {
    Select-Gpu4090Target 'Auto' 0 117 1000 85 128 'GPT' | Should Be 'Esp' }
  It 'creates a partition in free space only when none is labeled GPU4090' {
    Select-Gpu4090Target 'Auto' 0 50 128 85 128 'GPT' | Should Be 'FreeSpace' }
  It 'too small reuse, no ESP room: no second GPU4090 partition' {
    Select-Gpu4090Target 'Auto' 64 50 1000 85 128 'GPT' | Should Be $null }
  It 'too small reuse still falls back to a roomy ESP' {
    Select-Gpu4090Target 'Auto' 64 200 0 85 128 'GPT' | Should Be 'Esp' }
  It 'no room anywhere' {
    Select-Gpu4090Target 'Auto' 0 50 100 85 128 'GPT' | Should Be $null }
  It 'refuses FreeSpace when a GPU4090 partition exists' {
    { Select-Gpu4090Target 'FreeSpace' 124 0 1000 85 128 'GPT' } | Should Throw 'already exists' }
  It 'refuses NewPartition when a GPU4090 partition exists' {
    { Select-Gpu4090Target 'NewPartition' 124 0 1000 85 128 'GPT' } | Should Throw 'already exists' }
  It 'refuses Reuse without a GPU4090 partition' {
    { Select-Gpu4090Target 'Reuse' 0 0 0 85 128 'GPT' } | Should Throw 'No partition labeled GPU4090' }
  It 'refuses Reuse when too small' {
    { Select-Gpu4090Target 'Reuse' 64 0 0 85 128 'GPT' } | Should Throw '64 MB, 85 MB needed' }
  It 'passes explicit Esp and Volume through' {
    Select-Gpu4090Target 'Esp' 124 0 0 85 128 'GPT' | Should Be 'Esp'
    Select-Gpu4090Target 'Volume' 124 0 0 85 128 'GPT' | Should Be 'Volume' }
}

Describe 'Get-Gpu4090PendingCopies and Measure-Gpu4090CopyBytes' {
  # src\a.bin = 'AAAA', dst\a.bin identical; src\b.bin = 'BBBBBBBB', dst\b.bin = 'XX'; src\c.bin = 'CCCC', no dst\c.bin.
  # Hash holds the lower-case SHA-256 of the source, as SHA256SUMS does.
  $src = New-Item -ItemType Directory (Join-Path $TestDrive 'src')
  $dst = New-Item -ItemType Directory (Join-Path $TestDrive 'dst')
  $content = @{ 'a.bin' = 'AAAA'; 'b.bin' = 'BBBBBBBB'; 'c.bin' = 'CCCC' }
  foreach ($name in $content.Keys) { [IO.File]::WriteAllText((Join-Path $src $name), $content[$name]) }
  [IO.File]::WriteAllText((Join-Path $dst 'a.bin'), 'AAAA')
  [IO.File]::WriteAllText((Join-Path $dst 'b.bin'), 'XX')
  $files = foreach ($name in 'a.bin', 'b.bin', 'c.bin') {
    [pscustomobject]@{ Source = Join-Path $src $name; Dest = Join-Path $dst $name
      Hash = (Get-FileHash -Algorithm SHA256 (Join-Path $src $name)).Hash.ToLower() }
  }
  $pending = Get-Gpu4090PendingCopies $files

  It 'skips an identical file even with a lower-case hash'   { ($pending | ForEach-Object { Split-Path $_.Dest -Leaf }) -contains 'a.bin' | Should Be $false }
  It 'copies a file whose destination differs'               { ($pending | ForEach-Object { Split-Path $_.Dest -Leaf }) -contains 'b.bin' | Should Be $true }
  It 'copies a missing file'                                 { ($pending | ForEach-Object { Split-Path $_.Dest -Leaf }) -contains 'c.bin' | Should Be $true }
  It 'net bytes = sources of pending minus replaced destinations' { Measure-Gpu4090CopyBytes $pending | Should Be (8 + 4 - 2) }
}

Describe 'Get-Gpu4090FastStartup and Set-Gpu4090FastStartup' {
  # A scratch key under HKCU stands in for the Power key; HKLM is never touched.
  $key = "HKCU:\Software\Gpu4090UnlockTest-$([guid]::NewGuid())"
  New-Item -Path $key -Force | Out-Null
  try {
    It 'a missing value reads as not set' {
      $s = Get-Gpu4090FastStartup $key
      $s.Exists | Should Be $false
      $s.Value | Should Be $null }
    It 'a written DWORD reads back' {
      Set-Gpu4090FastStartup @{ Exists = $true; Value = 1 } $key
      $s = Get-Gpu4090FastStartup $key
      $s.Exists | Should Be $true
      $s.Value | Should Be 1 }
    It 'restoring a not-set state removes the value' {
      Set-Gpu4090FastStartup @{ Exists = $true; Value = 0 } $key
      Set-Gpu4090FastStartup ([pscustomobject]@{ Exists = $false; Value = $null }) $key
      (Get-Gpu4090FastStartup $key).Exists | Should Be $false }
    It 'restores a state read back from the JSON install record' {
      $before = ([ordered]@{ Exists = $true; Value = 1 } | ConvertTo-Json | ConvertFrom-Json)
      Set-Gpu4090FastStartup @{ Exists = $true; Value = 0 } $key
      Set-Gpu4090FastStartup $before $key
      (Get-Gpu4090FastStartup $key).Value | Should Be 1 }
    It 'refuses a value that is not a DWORD' {
      New-ItemProperty -LiteralPath $key -Name HiberbootEnabled -PropertyType String -Value '1' -Force | Out-Null
      { Get-Gpu4090FastStartup $key } | Should Throw 'not a DWORD' }
    It 'describes both states' {
      Format-Gpu4090FastStartup @{ Exists = $true; Value = 0 } | Should Be 'HiberbootEnabled=0'
      Format-Gpu4090FastStartup @{ Exists = $false; Value = $null } | Should Be 'HiberbootEnabled not set (Windows default: on)' }
  } finally { Remove-Item -Path $key -Recurse -Force }
}
