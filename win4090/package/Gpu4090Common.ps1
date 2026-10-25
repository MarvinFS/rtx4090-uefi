# Shared helpers for Install-, Uninstall- and Test-Gpu4090Unlock.ps1 (dot-sourced, Windows PowerShell 5.1+).
$Gpu4090Data = Join-Path $env:ProgramData 'Gpu4090Unlock'
$Gpu4090Record = Join-Path $Gpu4090Data 'install.json'
$Gpu4090EspType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$Gpu4090EfiPath = '\EFI\GPU4090\UNLOCK4090.EFI'
# What the installer leaves in $Gpu4090Data so the check and uninstall work without the package.
$Gpu4090Tools = @('Gpu4090Common.ps1', 'Test-Gpu4090Unlock.ps1', 'Uninstall-Gpu4090Unlock.ps1', 'Check.cmd', 'Uninstall.cmd',
  'SHA256SUMS', 'ASSETS.LIST', 'README.md', 'tools')

function Save-Gpu4090Record($fields) {
  New-Item -ItemType Directory -Force $Gpu4090Data | Out-Null
  [pscustomobject]$fields | ConvertTo-Json | Set-Content $Gpu4090Record
}

# Entry ids in the firmware boot order ({fwbootmgr} displayorder), lower case, first = booted first.
function Get-FirmwareBootOrder {
  $order = @(); $in = $false
  foreach ($l in (& bcdedit /enum '{fwbootmgr}')) {
    if ($l -match '^displayorder\s+(\{[^}]+\})') { $in = $true; $order += $Matches[1].ToLower(); continue }
    if ($in -and $l -match '^\s+(\{[^}]+\})') { $order += $Matches[1].ToLower(); continue }
    $in = $false
  }
  , $order
}

function Get-BootEntryValue([string]$Id, [string]$Name) {
  foreach ($l in (& bcdedit /enum $Id)) { if ($l -match "^$Name\s+(.+)$") { return $Matches[1].Trim() } }
}

function Get-FreeDriveLetter { 72..90 | ForEach-Object { [char]$_ } | Where-Object { -not (Test-Path "$($_):\") } | Select-Object -First 1 }

# Gives the install partition a temporary letter when it has none (the booted ESP through mountvol /s).
function Mount-Gpu4090Volume($Part) {
  if ($Part.DriveLetter) { return [pscustomobject]@{ Root = "$($Part.DriveLetter):\"; Letter = $null } }
  $letter = Get-FreeDriveLetter
  if (-not $letter) { throw 'No free drive letter to mount the install partition.' }
  $esp = $Part.GptType -eq $Gpu4090EspType
  if ($esp) { & mountvol "${letter}:" /s; if ($LASTEXITCODE) { throw 'mountvol /s failed.' } }
  else { Add-PartitionAccessPath -DiskNumber $Part.DiskNumber -PartitionNumber $Part.PartitionNumber -AccessPath "${letter}:\" }
  [pscustomobject]@{ Root = "${letter}:\"; Letter = $letter; Esp = $esp; Disk = $Part.DiskNumber; Number = $Part.PartitionNumber }
}

function Dismount-Gpu4090Volume($Volume) {
  if (-not $Volume -or -not $Volume.Letter) { return }
  if ($Volume.Esp) { & mountvol "$($Volume.Letter):" /d }
  else { Remove-PartitionAccessPath -DiskNumber $Volume.Disk -PartitionNumber $Volume.Number -AccessPath "$($Volume.Letter):\" }
}
# The FAT32 label of the installer's own partition; a later install finds the partition again by it and reuses it.
$Gpu4090Label = 'GPU4090'
# The one partition on the disk labeled $Gpu4090Label (never the ESP), or $null.
function Find-Gpu4090Partition([int]$DiskNumber) {
  $found = @(Get-Partition -DiskNumber $DiskNumber | Where-Object {
    $v = Get-Volume -Partition $_ -ErrorAction SilentlyContinue
    $_.GptType -ne $Gpu4090EspType -and $v.FileSystem -eq 'FAT32' -and $v.FileSystemLabel -eq $Gpu4090Label })
  if ($found.Count -gt 1) {
    throw "Found $($found.Count) partitions labeled $Gpu4090Label on disk $DiskNumber (partitions $($found.PartitionNumber -join ', ')). Keep one: delete or relabel the others in Disk Management."
  }
  $found | Select-Object -First 1
}
# Where to install. Auto prefers a big enough $Gpu4090Label partition, then the ESP (32 MB headroom), then unallocated
# space, and never makes a second $Gpu4090Label partition; $null means Auto found no room. ReuseMB 0 = no such partition.
function Select-Gpu4090Target([string]$Requested, [int]$ReuseMB, [int]$EspFreeMB, [int]$FreeExtentMB, [int]$NeededMB, [int]$NewMB, [string]$PartitionStyle) {
  if ($Requested -eq 'Auto') {
    if ($ReuseMB -gt 0 -and $ReuseMB -ge $NeededMB) { return 'Reuse' }
    if ($EspFreeMB -ge $NeededMB + 32) { return 'Esp' }
    if ($ReuseMB -eq 0 -and $PartitionStyle -eq 'GPT' -and $FreeExtentMB -ge $NewMB) { return 'FreeSpace' }
    return $null
  }
  if ($Requested -eq 'Reuse') {
    if ($ReuseMB -eq 0) { throw "No partition labeled $Gpu4090Label on the Windows boot disk." }
    if ($ReuseMB -lt $NeededMB) { throw "The $Gpu4090Label partition has $ReuseMB MB, $NeededMB MB needed." }
  }
  if ($Requested -in 'FreeSpace', 'NewPartition' -and $ReuseMB -gt 0) {
    throw "A $Gpu4090Label partition already exists on this disk. Use -Target Reuse, or delete it in Disk Management first."
  }
  $Requested
}
# The files (Source, Dest, Hash = lower-case SHA-256) whose destination is missing or holds different bytes.
function Get-Gpu4090PendingCopies($Files) {
  $Files | Where-Object {
    -not (Test-Path -LiteralPath $_.Dest) -or (Get-FileHash -LiteralPath $_.Dest -Algorithm SHA256).Hash.ToLower() -ne $_.Hash.ToLower() }
}
# Extra bytes the copy needs on the target: the pending sources minus the destination files they replace.
function Measure-Gpu4090CopyBytes($Pending) {
  [long]$bytes = 0
  foreach ($f in $Pending) {
    $bytes += (Get-Item -LiteralPath $f.Source).Length
    if (Test-Path -LiteralPath $f.Dest) { $bytes -= (Get-Item -LiteralPath $f.Dest).Length }
  }
  $bytes
}
