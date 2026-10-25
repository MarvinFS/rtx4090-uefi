#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Installs the RTX 4090 pre-boot unlock: puts the EFI app and its firmware files on a FAT volume and adds a
  firmware boot entry in front of Windows Boot Manager. Works with any disk layout; it looks before it decides.
.DESCRIPTION
  DEFAULT IS A DRY RUN: it inspects the PC and prints exactly what it would do. Add -Apply to do it.
  Needs about 85 MB for the firmware files (the app itself is under 100 KB). With -Target Auto (default):
    1. A FAT32 partition labeled GPU4090 on the Windows boot disk, left by an earlier install (uninstall keeps it).
    2. Otherwise the EFI System Partition if it has room (needed + 32 MB headroom, so Windows servicing keeps working).
    3. Otherwise a new small FAT32 partition in unallocated space that already exists on the same disk.
    4. Otherwise NOTHING is changed: it tells you how much space to free (shrink C: in Disk Management while Windows
       runs, no restart, leave the space unallocated) and you run it again. It never shrinks a volume unless you ask with -Target NewPartition.
  Files already on the target with the same SHA-256 are kept, not copied again.
  The GPU firmware (VBIOS) is never touched. The unlock is volatile (gone after a full power-off), so the app runs on every boot.
.PARAMETER Target
  Auto (default) | Reuse (the GPU4090 partition an earlier install left) | Esp | FreeSpace | NewPartition (shrinks C: by -NewPartitionMB)
  | Volume (an existing FAT32 volume, with -VolumeLetter)
.PARAMETER Variant
  Auto   unlock after a 3 s countdown (Esc opens the menu)                                  [default]
  Manual same program, but the countdown ends in plain Windows; press U (or menu 1) to unlock
.EXAMPLE
  .\Install-Gpu4090Unlock.ps1                 # inspect and show the plan
  .\Install-Gpu4090Unlock.ps1 -Apply          # do it
#>
[CmdletBinding()]
param(
  [string]$PackageDir,
  [ValidateSet('Auto', 'Manual')][string]$Variant = 'Auto',
  [ValidateSet('Auto', 'Reuse', 'Esp', 'FreeSpace', 'NewPartition', 'Volume')][string]$Target = 'Auto',
  [string]$VolumeLetter,
  [int]$NewPartitionMB = 0,
  [switch]$Apply
)
$ErrorActionPreference = 'Stop'
if (-not $PackageDir) { $PackageDir = $PSScriptRoot }   # Windows PowerShell 5.1 leaves $PSScriptRoot empty in param defaults
. (Join-Path $PSScriptRoot 'Gpu4090Common.ps1')
$Record = $Gpu4090Record
$SystemLetter = $env:SystemDrive.TrimEnd(':')
function Step($text) { if ($Apply) { Write-Host "[do]   $text" } else { Write-Host "[plan] $text" } }
function Get-FreeLetter { Get-FreeDriveLetter }

# --- 1. This PC can run it ------------------------------------------------------------------------
try { $secureBoot = Confirm-SecureBootUEFI } catch { throw 'This PC is not booted in UEFI mode (or the Secure Boot state is unreadable). The unlock needs UEFI.' }
if ($secureBoot) { throw 'Secure Boot is ON. The unlock app is unsigned: turn Secure Boot off in the firmware setup first.' }
$gpus = @(Get-CimInstance Win32_VideoController | Where-Object { $_.PNPDeviceID -like 'PCI\VEN_10DE&DEV_2684*' })
if ($gpus.Count -ne 1) { throw "Expected exactly one GeForce RTX 4090 (10de:2684), found $($gpus.Count). Not installing." }
Write-Host "GPU: $($gpus[0].Name)  $($gpus[0].PNPDeviceID)"
if (Test-Path $Record) { throw "Already installed (record at $Record). Run Uninstall-Gpu4090Unlock.ps1 first." }
try {   # a new boot component can make BitLocker ask for its recovery key once
  $bl = Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop
  if ($bl.ProtectionStatus -eq 'On') { Write-Warning "BitLocker is ON for $env:SystemDrive. Adding a boot entry may make the next start ask for the recovery key. Have the key ready, or run: Suspend-BitLocker -MountPoint $env:SystemDrive -RebootCount 2" }
} catch { }

# --- 2. The package is complete and unmodified (transport check, once, at install time) -----------
$efiName = if ($Variant -eq 'Auto') { 'UNLOCK4090.EFI' } else { 'UNLOCK4090M.EFI' }
$sums = Join-Path $PackageDir 'SHA256SUMS'
if (-not (Test-Path $sums)) { throw "SHA256SUMS missing in $PackageDir" }
$assets = @(Get-Content (Join-Path $PackageDir 'ASSETS.LIST'))
$need = @($efiName) + $assets
$want = @{}   # package file -> its SHA-256, what the copies on the target must match
foreach ($line in Get-Content $sums) {
  if ($line -notmatch '^([0-9a-f]{64})\s+\*?(.+)$') { continue }
  $rel = $Matches[2] -replace '/', '\'
  if ($need -notcontains $rel) { continue }
  $path = Join-Path $PackageDir $rel
  if (-not (Test-Path $path)) { throw "Missing package file: $rel" }
  if ((Get-FileHash $path -Algorithm SHA256).Hash.ToLower() -ne $Matches[1]) { throw "Hash mismatch: $rel" }
  $want[$rel] = $Matches[1]
}
foreach ($a in $need) { if (-not $want.ContainsKey($a)) { throw "SHA256SUMS has no line for $a" } }
$bytes = ($need | ForEach-Object { (Get-Item (Join-Path $PackageDir $_)).Length } | Measure-Object -Sum).Sum
$neededMB = [math]::Ceiling($bytes / 1MB) + 4
Write-Host "Package verified: $($need.Count) files, about $neededMB MB needed on the target."

# --- 3. Look at the machine ---------------------------------------------------------------------------
$espLetter = $null; $reuseVol = $null; $drive = $null; $partGuid = $null; $entry = $null
try {
  $espLetter = Get-FreeLetter
  & mountvol "${espLetter}:" /s            # mounts the EFI System Partition Windows booted from
  if ($LASTEXITCODE) { throw 'Could not mount the EFI System Partition (mountvol /s).' }
  # Storage cmdlets do not report a drive letter mounted with mountvol /s on an ESP: match the volume name instead.
  $espVolume = ((& mountvol "${espLetter}:" /L) | Out-String).Trim()
  $espPart = Get-Partition | Where-Object { $_.AccessPaths -contains "${espLetter}:\" -or $_.AccessPaths -contains $espVolume } | Select-Object -First 1
  if (-not $espPart) { throw "Mounted the EFI System Partition as ${espLetter}: ($espVolume) but could not find its partition." }
  $espFreeMB = [math]::Floor(([IO.DriveInfo]::new("${espLetter}:\")).AvailableFreeSpace / 1MB)
  $diskNo = $espPart.DiskNumber
  $disk = Get-Disk -Number $diskNo
  $sector = $disk.LogicalSectorSize
  $minPartMB = if ($sector -ge 4096) { 272 } else { 64 }      # FAT32 needs >= 65525 clusters: 260 MB on 4K-sector disks
  $newMB = [math]::Max($neededMB + 24, [math]::Max($minPartMB, 128))
  $freeExtentMB = [math]::Floor($disk.LargestFreeExtent / 1MB)
  $reusePart = Find-Gpu4090Partition $diskNo      # left by an earlier install; uninstall keeps it
  $reuseMB = if ($reusePart) { [math]::Floor((Get-Volume -Partition $reusePart).Size / 1MB) } else { 0 }
  Write-Host ("Windows boot disk {0}: {1}, {2}-byte sectors, ESP free {3} MB, largest unallocated extent {4} MB, GPU4090 partition {5}" -f $diskNo, $disk.PartitionStyle, $sector, $espFreeMB, $freeExtentMB, $(if ($reusePart) { "$reuseMB MB (partition $($reusePart.PartitionNumber))" } else { 'none' }))

  # --- 4. Choose ---------------------------------------------------------------------------------------
  $choice = Select-Gpu4090Target $Target $reuseMB $espFreeMB $freeExtentMB $neededMB $newMB $disk.PartitionStyle
  if (-not $choice) {
    Write-Host ''
    Write-Host "No suitable space. The ESP has $espFreeMB MB free ($($neededMB + 32) MB needed) and the largest unallocated extent is $freeExtentMB MB ($newMB MB needed)."
    if ($reuseMB -gt 0) { Write-Host "The GPU4090 partition is $reuseMB MB, $neededMB MB needed: delete it in Disk Management and run this again." }
    Write-Host 'Nothing was changed. To make room:'
    Write-Host "  1. Open Disk Management (diskmgmt.msc), right-click $env:SystemDrive, Shrink Volume..., enter $newMB (MB)."
    Write-Host '     Shrink Volume works on the running system; no restart is needed.'
    Write-Host '  2. Leave the new space UNALLOCATED (do not create or format a volume in it).'
    Write-Host '  3. Run this script again.'
    Write-Host "If Windows cannot shrink that far (unmovable files), try again after: powercfg /h off  (and a restart), or let this script try: -Target NewPartition -NewPartitionMB $newMB -Apply"
    exit 2
  }
  if ($Target -eq 'Auto') { Write-Host "Chosen automatically: $choice" }

  # --- 5. Prepare the target volume ---------------------------------------------------------------------
  switch ($choice) {
    'Reuse' {
      Step "reuse partition $($reusePart.PartitionNumber) on disk $diskNo (FAT32, label GPU4090, $reuseMB MB) from an earlier install: no partition changes"
      if ($Apply) { $reuseVol = Mount-Gpu4090Volume $reusePart; $drive = $reuseVol.Root.TrimEnd('\') } else { $drive = 'X:' }
      $partGuid = $reusePart.Guid
    }
    'Esp' {
      if ($espFreeMB -lt $neededMB + 32) { throw "The ESP has $espFreeMB MB free, $($neededMB + 32) MB needed (including headroom for Windows updates)." }
      $drive = "${espLetter}:"; $partGuid = $espPart.Guid
      Step "use the EFI System Partition (disk $diskNo, partition $($espPart.PartitionNumber)): no partition changes"
    }
    'FreeSpace' {
      if ($disk.PartitionStyle -ne 'GPT') { throw 'The disk is not GPT; a UEFI Windows boot disk should be. Not creating partitions on it.' }
      if ($freeExtentMB -lt $newMB) { throw "The largest unallocated extent is $freeExtentMB MB, $newMB MB needed." }
      Step "create a $newMB MB GPT partition in the existing unallocated space on disk $diskNo, format FAT32, label GPU4090"
      if ($Apply) {
        $np = New-Partition -DiskNumber $diskNo -Size ($newMB * 1MB) -AssignDriveLetter
        Format-Volume -Partition $np -FileSystem FAT32 -NewFileSystemLabel 'GPU4090' -Confirm:$false | Out-Null
        $np = Get-Partition -DiskNumber $diskNo -PartitionNumber $np.PartitionNumber
        if (-not $np.DriveLetter) { Add-PartitionAccessPath -DiskNumber $diskNo -PartitionNumber $np.PartitionNumber -AssignDriveLetter; $np = Get-Partition -DiskNumber $diskNo -PartitionNumber $np.PartitionNumber }
        if (-not $np.DriveLetter) { throw "Created partition $($np.PartitionNumber) on disk $diskNo but it has no drive letter." }
        $drive = "$($np.DriveLetter):"; $partGuid = $np.Guid
      } else { $drive = 'X:' }
    }
    'NewPartition' {
      if ($disk.PartitionStyle -ne 'GPT') { throw 'The disk is not GPT. Not creating partitions on it.' }
      $mb = if ($NewPartitionMB -gt 0) { $NewPartitionMB } else { $newMB }
      if ($mb -lt $newMB) { throw "-NewPartitionMB $mb is too small (need $newMB MB on this disk)." }
      $c = Get-Partition -DriveLetter $SystemLetter
      if ($c.DiskNumber -ne $diskNo) { throw "$env:SystemDrive is on another disk than the ESP; shrink a volume with Disk Management and run again." }
      $sup = Get-PartitionSupportedSize -DriveLetter $SystemLetter
      if (($c.Size - $sup.SizeMin) -lt (($mb + 64) * 1MB)) { throw "$env:SystemDrive cannot be shrunk by $mb MB right now (unmovable files). Use Disk Management, or free space and retry." }
      Step "shrink $env:SystemDrive by $mb MB"
      Step "create a $mb MB GPT partition on disk $diskNo, format FAT32, label GPU4090"
      if ($Apply) {
        Resize-Partition -DriveLetter $SystemLetter -Size ($c.Size - $mb * 1MB)
        $np = New-Partition -DiskNumber $diskNo -Size ($mb * 1MB) -AssignDriveLetter
        Format-Volume -Partition $np -FileSystem FAT32 -NewFileSystemLabel 'GPU4090' -Confirm:$false | Out-Null
        $np = Get-Partition -DiskNumber $diskNo -PartitionNumber $np.PartitionNumber
        if (-not $np.DriveLetter) { Add-PartitionAccessPath -DiskNumber $diskNo -PartitionNumber $np.PartitionNumber -AssignDriveLetter; $np = Get-Partition -DiskNumber $diskNo -PartitionNumber $np.PartitionNumber }
        if (-not $np.DriveLetter) { throw "Created partition $($np.PartitionNumber) on disk $diskNo but it has no drive letter." }
        $drive = "$($np.DriveLetter):"; $partGuid = $np.Guid
      } else { $drive = 'X:' }
    }
    'Volume' {
      if (-not $VolumeLetter) { throw '-Target Volume needs -VolumeLetter, e.g. -VolumeLetter E:' }
      $vl = $VolumeLetter.TrimEnd(':')
      $vol = Get-Volume -DriveLetter $vl
      if ($vol.FileSystem -ne 'FAT32') { throw "${vl}: is $($vol.FileSystem), not FAT32. The firmware must be able to read it." }
      if ($vol.SizeRemaining / 1MB -lt $neededMB) { throw "${vl}: has too little free space ($neededMB MB needed)." }
      $part = Get-Partition -DriveLetter $vl
      if ((Get-Disk -Number $part.DiskNumber).BusType -eq 'USB') { Write-Warning 'This volume is on a USB disk: the unlock only works while it stays plugged in.' }
      $drive = "${vl}:"; $partGuid = $part.Guid; $diskNo = $part.DiskNumber
      Step "use existing FAT32 volume ${vl}:"
    }
  }

  # Recorded as soon as something exists, so Uninstall can clean up a half-finished install.
  $rec = [ordered]@{ entry = $null; target = $choice; disk = $diskNo; partitionGuid = $partGuid; variant = $Variant }
  if ($Apply) { Save-Gpu4090Record $rec }

  # --- 6. Files ---------------------------------------------------------------------------------------------
  Step "copy $efiName to $drive\EFI\GPU4090\UNLOCK4090.EFI and the five firmware files to $drive\GPU4090-ASSETS\ (files already there with the same SHA-256 are kept)"
  if ($Apply) {
    $files = foreach ($a in $need) {
      $dest = if ($a -eq $efiName) { "$drive\EFI\GPU4090\UNLOCK4090.EFI" } else { "$drive\GPU4090-ASSETS\$(Split-Path $a -Leaf)" }
      [pscustomobject]@{ Source = Join-Path $PackageDir $a; Dest = $dest; Hash = $want[$a] }
    }
    $pending = @(Get-Gpu4090PendingCopies $files)
    $pendingDest = @($pending | ForEach-Object { $_.Dest })
    foreach ($f in $files) { if ($pendingDest -notcontains $f.Dest) { Write-Host "[keep] $($f.Dest) (same SHA-256)" } }
    $freeBytes = ([IO.DriveInfo]::new("$drive\")).AvailableFreeSpace
    $moreBytes = Measure-Gpu4090CopyBytes $pending
    if ($moreBytes -gt $freeBytes) { throw ("$drive has {0} MB free, {1} MB more needed for the files that differ." -f [math]::Floor($freeBytes / 1MB), [math]::Ceiling($moreBytes / 1MB)) }
    New-Item -ItemType Directory -Force "$drive\EFI\GPU4090", "$drive\GPU4090-ASSETS" | Out-Null
    foreach ($f in $pending) { Copy-Item -LiteralPath $f.Source $f.Dest -Force }
    foreach ($f in $pending) {   # read back what the firmware will read
      if ((Get-FileHash -LiteralPath $f.Dest -Algorithm SHA256).Hash.ToLower() -ne $f.Hash) { throw "Copy verification failed: $($f.Dest)" }
    }
  }

  # --- 7. Firmware boot entry in front of Windows Boot Manager --------------------------------------
  Step "add a firmware boot entry 'RTX 4090 unlock' (bcdedit /copy {bootmgr}) pointing at \EFI\GPU4090\UNLOCK4090.EFI on $drive"
  Step 'put it first in the firmware display order; Windows Boot Manager stays in the list as the fallback'
  if ($Apply) {
    $out = & bcdedit /copy '{bootmgr}' /d 'RTX 4090 unlock'
    $entry = [regex]::Match(($out -join ' '), '\{[0-9a-fA-F-]{36}\}').Value
    if (-not $entry) { throw "bcdedit did not return an entry id: $out" }
    $rec.entry = $entry; Save-Gpu4090Record $rec
    & bcdedit /set $entry path $Gpu4090EfiPath; if ($LASTEXITCODE) { throw 'bcdedit set path failed' }
    & bcdedit /set $entry device "partition=$drive"; if ($LASTEXITCODE) { throw 'bcdedit set device failed' }
    & bcdedit /set '{fwbootmgr}' displayorder $entry /addfirst; if ($LASTEXITCODE) { throw 'bcdedit displayorder failed' }
    # Read the result back from the firmware store instead of trusting the exit codes.
    $order = Get-FirmwareBootOrder
    if ($order.Count -eq 0 -or $order[0] -ne $entry.ToLower()) { throw "The new entry $entry is not first in the firmware boot order: $($order -join ', ')" }
    if ((Get-BootEntryValue $entry 'path') -ne $Gpu4090EfiPath) { throw "The new entry $entry does not point at $Gpu4090EfiPath" }
    Write-Host "Firmware boot order: $($order -join ', ')"
  }

  # --- 8. Keep a new partition out of Explorer (no letter, so no low-disk-space warnings for it) ----
  if ($choice -in 'FreeSpace', 'NewPartition') {
    Step "remove the drive letter from the new partition and keep Windows from assigning one (the boot entry does not use letters)"
    if ($Apply) {
      $np = Get-Partition | Where-Object { $_.Guid -eq $partGuid }
      Set-Partition -DiskNumber $np.DiskNumber -PartitionNumber $np.PartitionNumber -NoDefaultDriveLetter $true
      Remove-PartitionAccessPath -DiskNumber $np.DiskNumber -PartitionNumber $np.PartitionNumber -AccessPath "$drive\"
    }
  }

  # --- 9. Check and uninstall tools stay on this PC ---------------------------------------------------
  Step "copy the check and uninstall tools to $Gpu4090Data"
  if ($Apply) {
    foreach ($t in $Gpu4090Tools) { $p = Join-Path $PackageDir $t; if (Test-Path $p) { Copy-Item $p $Gpu4090Data -Recurse -Force } }
    Write-Host "`nInstalled. Restart. On the next boot you get a 3 second countdown; Esc opens the menu."
    Write-Host 'If the screen does not come back, power off, then use the firmware boot menu to pick Windows Boot Manager.'
    Write-Host "After the restart, check it: $Gpu4090Data\Check.cmd"
  } else {
    Write-Host "`nNothing was changed. Run again with -Apply to perform the steps above."
  }
} finally {
  Dismount-Gpu4090Volume $reuseVol
  if ($espLetter -and (Test-Path "${espLetter}:\")) { & mountvol "${espLetter}:" /d }
}
