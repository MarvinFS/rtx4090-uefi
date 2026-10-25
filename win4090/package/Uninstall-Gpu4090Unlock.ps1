#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Removes the RTX 4090 pre-boot unlock: deletes its firmware boot entry and its files, and puts Windows Fast Startup
  back to its value before the install if the installer turned it off.
.DESCRIPTION
  DEFAULT IS A DRY RUN; add -Apply to make the changes. Windows Boot Manager is never modified.
  Only the program's folders are deleted; no partition is deleted or resized. A partition labeled GPU4090 stays
  for the next install to reuse; delete it in Disk Management to get the space back.
#>
[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Gpu4090Common.ps1')
function Step($text) { if ($Apply) { Write-Host "[do]   $text" } else { Write-Host "[plan] $text" } }
if (-not (Test-Path $Gpu4090Record)) { throw "No install record at $Gpu4090Record. Nothing to remove (or it was installed by hand: delete the 'RTX 4090 unlock' entry with bcdedit /enum firmware and bcdedit /delete)." }
$r = Get-Content $Gpu4090Record -Raw | ConvertFrom-Json

if ($r.entry) {
  Step "remove firmware boot entry $($r.entry)"
  if ($Apply) {
    & bcdedit /set '{fwbootmgr}' displayorder $r.entry /remove | Out-Null
    & bcdedit /delete $r.entry | Out-Null
    if ((Get-FirmwareBootOrder) -contains $r.entry.ToLower()) { throw "The firmware boot entry $($r.entry) is still in the boot order." }
  }
} else { Write-Host 'The install record has no boot entry (the install stopped before creating it).' }

$part = Get-Partition -DiskNumber $r.disk | Where-Object { $_.Guid -eq $r.partitionGuid } | Select-Object -First 1
if (-not $part) { Write-Warning 'The partition that held the files is gone; only the boot entry was removed.' }
else {
  Step "delete \EFI\GPU4090, \GPU4090-ASSETS and \GPU4090-UNLOCK on disk $($r.disk) partition $($part.PartitionNumber) (the partition stays)"
  if ($Apply) {
    $v = Mount-Gpu4090Volume $part
    try { foreach ($d in 'EFI\GPU4090', 'GPU4090-ASSETS', 'GPU4090-UNLOCK') { Remove-Item (Join-Path $v.Root $d) -Recurse -Force -ErrorAction SilentlyContinue } }
    finally { Dismount-Gpu4090Volume $v }
  }
}

# Records from v0.1.0 have no Fast Startup fields: nothing to restore.
if ($r.fastStartupChanged) {
  $now = Get-Gpu4090FastStartup
  if ($now.Exists -and $now.Value -eq 0) {
    Step "restore Windows Fast Startup to its value before the install ($(Format-Gpu4090FastStartup $r.fastStartupBefore))"
    if ($Apply) { Set-Gpu4090FastStartup $r.fastStartupBefore }
  } else { Write-Host "Fast Startup was changed after the install ($(Format-Gpu4090FastStartup $now)); leaving it as it is." }
}

Step "delete $Gpu4090Data (install record, check and uninstall tools)"
if ($Apply) {
  Remove-Item $Gpu4090Data -Recurse -Force -ErrorAction SilentlyContinue
  Write-Host 'Removed. The next boot goes straight to Windows Boot Manager.'
  if ($part -and $r.target -in 'FreeSpace', 'NewPartition', 'Reuse') {
    Write-Host "The $Gpu4090Label partition (disk $($r.disk) partition $($part.PartitionNumber)) stays for the next install to reuse. To get its space back, delete it in Disk Management."
  }
} else { Write-Host "`nNothing was changed. Run again with -Apply to perform the steps above." }
