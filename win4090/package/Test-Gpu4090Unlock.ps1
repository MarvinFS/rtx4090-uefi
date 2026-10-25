#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Checks the installed RTX 4090 pre-boot unlock and whether the GPU is unlocked in this Windows session.
.DESCRIPTION
  Changes nothing. Reads the install record, the firmware boot order, the files on the install partition
  (giving it a temporary drive letter if it has none, removed again), the app's newest boot log and the
  NVIDIA driver state. -Benchmark also runs the bundled tensor issue-rate test (tools\mma_bench.exe, about
  a minute of full GPU load): unlocked, FP16 and BF16 with FP32 accumulate run at the same rate as FP16 with
  FP16 accumulate; on a stock card they run at half that rate. That comparison needs no earlier baseline.
  Output is also saved to ProgramData\Gpu4090Unlock\check-<time>.log. Exit code = number of failed checks.
#>
[CmdletBinding()]
param([string]$PackageDir, [switch]$Benchmark)
$ErrorActionPreference = 'Stop'
if (-not $PackageDir) { $PackageDir = $PSScriptRoot }   # Windows PowerShell 5.1 leaves $PSScriptRoot empty in param defaults
. (Join-Path $PSScriptRoot 'Gpu4090Common.ps1')
$script:failed = 0
function Ok($t) { Write-Host "[ok]   $t" }
function Bad($t) { Write-Host "[FAIL] $t"; $script:failed++ }
function Note($t) { Write-Host "[info] $t" }

New-Item -ItemType Directory -Force $Gpu4090Data | Out-Null
Start-Transcript -Path (Join-Path $Gpu4090Data "check-$(Get-Date -Format yyyyMMdd-HHmmss).log") | Out-Null
try {
  $boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
  Note "Windows started $boot"

  # --- install record and firmware boot entry ------------------------------------------------------
  if (-not (Test-Path $Gpu4090Record)) { Bad "not installed: no $Gpu4090Record"; exit 1 }
  $r = Get-Content $Gpu4090Record -Raw | ConvertFrom-Json
  Ok "installed: target $($r.target), disk $($r.disk), variant $($r.variant)"
  if (-not $r.entry) { Bad 'the install record has no boot entry (install did not finish)' }
  else {
    $order = Get-FirmwareBootOrder
    $path = Get-BootEntryValue $r.entry 'path'
    if ($path -ne $Gpu4090EfiPath) { Bad "boot entry $($r.entry) is missing or points elsewhere ($path)" }
    elseif ($order.Count -gt 0 -and $order[0] -eq $r.entry.ToLower()) { Ok "boot entry 'RTX 4090 unlock' is first in the firmware boot order" }
    else {
      Bad "boot entry $($r.entry) exists but is not first (order: $($order -join ', '))"
      Note "A BIOS or Windows update can reorder entries. Fix: bcdedit /set {fwbootmgr} displayorder $($r.entry) /addfirst"
    }
  }

  # --- Windows Fast Startup (off, unless the install was told to keep it) ------------------------
  $fs = Get-Gpu4090FastStartup
  $fsPolicy = Get-Gpu4090FastStartup $Gpu4090FastStartupPolicyKey
  $fsOn = ($fsPolicy.Exists -and $fsPolicy.Value -ne 0) -or -not ($fs.Exists -and $fs.Value -eq 0)
  if (-not $fsOn) { Ok 'Windows Fast Startup is off' }
  elseif ($r.fastStartupKept) { Note "Windows Fast Startup is on, as installed with -KeepFastStartup ($(Format-Gpu4090FastStartup $fs)): after a shut down and start the card may be stock; restart instead" }
  elseif ($r.fastStartupChanged) {
    Bad "Windows Fast Startup is on again ($(Format-Gpu4090FastStartup $fs)$(if ($fsPolicy.Exists) { ', Group Policy ' + (Format-Gpu4090FastStartup $fsPolicy) }))"
    Note 'A Windows update or a policy can turn it back on. Fix: powercfg /h off, or Control Panel, Power Options, Choose what the power buttons do'
  } else { Note "Windows Fast Startup is on ($(Format-Gpu4090FastStartup $fs)): after a shut down and start the card may be stock; restart instead" }

  # --- files on the install partition and the app's boot log -------------------------------------
  $part = Get-Partition | Where-Object { $_.Guid -eq $r.partitionGuid } | Select-Object -First 1
  if (-not $part) { Bad "install partition $($r.partitionGuid) not found" }
  else {
    $v = Mount-Gpu4090Volume $part
    try {
      $root = $v.Root
      $efiName = if ($r.variant -eq 'Manual') { 'UNLOCK4090M.EFI' } else { 'UNLOCK4090.EFI' }
      $sumsFile = Join-Path $PackageDir 'SHA256SUMS'
      $assets = @(Get-Content (Join-Path $PackageDir 'ASSETS.LIST'))
      $want = @{}
      foreach ($line in Get-Content $sumsFile) { if ($line -match '^([0-9a-f]{64})\s+\*?(.+)$') { $want[($Matches[2] -replace '/', '\')] = $Matches[1] } }
      $files = @(@{ rel = $efiName; dest = Join-Path $root $Gpu4090EfiPath.TrimStart('\') }) +
               @($assets | ForEach-Object { @{ rel = $_; dest = Join-Path $root "GPU4090-ASSETS\$(Split-Path $_ -Leaf)" } })
      $bad = @($files | Where-Object { -not (Test-Path $_.dest) -or (Get-FileHash $_.dest -Algorithm SHA256).Hash.ToLower() -ne $want[$_.rel] })
      if ($bad.Count) { Bad "files missing or changed on the install partition: $(($bad | ForEach-Object { $_.rel }) -join ', ')" }
      else { Ok "the app and its $($assets.Count) firmware files are intact on the install partition" }

      $log = Get-ChildItem (Join-Path $root 'GPU4090-UNLOCK') -Filter 'RUN*.TXT' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
      if (-not $log) { Bad 'the app has written no boot log yet (it has not run from this partition: restart first)' }
      else {
        $text = Get-Content $log.FullName -Raw
        $age = ($boot - $log.LastWriteTime).TotalMinutes
        $thisBoot = $age -ge -2 -and $age -le 15
        Note "newest app log: $($log.Name), written $($log.LastWriteTime)$(if ($thisBoot) { ' (this boot)' } else { ' (NOT this boot)' })"
        if (-not $thisBoot) { Bad 'the app did not run at this boot (the firmware started Windows Boot Manager directly, or the boot order changed)' }
        elseif ($text -match 'TRANSACTION OK' -and $text -match 'POST-GOP SS0=0x88888888 SS1=0x8') { Ok 'the app unlocked the GPU at this boot (TRANSACTION OK, POST-GOP SS0=0x88888888 SS1=0x8)' }
        elseif ($text -match 'choice: stock') { Note 'stock Windows was chosen in the menu at this boot (no unlock attempted)' }
        else {
          Bad 'the app did not report a successful unlock at this boot; its last lines:'
          ($text -split "`r?`n" | Where-Object { $_ } | Select-Object -Last 12) | ForEach-Object { Write-Host "         $_" }
        }
      }
    } finally { Dismount-Gpu4090Volume $v }
  }

  # --- NVIDIA driver ----------------------------------------------------------------------------------
  $smi = & nvidia-smi --query-gpu=name,pci.device_id,driver_version,power.limit,pstate --format=csv,noheader 2>&1
  if ($LASTEXITCODE) { Bad "nvidia-smi failed: $smi" } else { Ok "driver: $smi" }
  $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'nvlddmkm'; StartTime = $boot } -ErrorAction SilentlyContinue | Where-Object { $_.Level -le 3 })
  if ($events.Count) { Bad "$($events.Count) NVIDIA driver errors or warnings since boot (Event Viewer, System, source nvlddmkm); first: $($events[-1].TimeCreated) id $($events[-1].Id)" }
  else { Ok 'no NVIDIA driver errors since boot' }

  # --- optional: measured tensor issue rate ----------------------------------------------------------
  if ($Benchmark) {
    $exe = Join-Path $PackageDir 'tools\mma_bench.exe'
    Note 'running tools\mma_bench.exe 20000 5 (about a minute of full GPU load) ...'
    $out = & $exe 20000 5 2>&1
    $out | ForEach-Object { Write-Host "         $_" }
    $rate = @{}
    foreach ($l in $out) { if ("$l" -match '^(f16/f16|f16/f32|bf16/f32)\s+([\d.]+)') { $rate[$Matches[1]] = [double]$Matches[2] } }
    if ($LASTEXITCODE -or $rate.Count -ne 3) { Bad 'the benchmark did not produce results' }
    else {
      $ratio = [math]::Min($rate['f16/f32'], $rate['bf16/f32']) / $rate['f16/f16']
      $msg = 'FP32-accumulate rate {0:N0} / {1:N0} TF/s (FP16/BF16) vs FP16-accumulate {2:N0} TF/s, ratio {3:N2}' -f $rate['f16/f32'], $rate['bf16/f32'], $rate['f16/f16'], $ratio
      if ($ratio -ge 0.85) { Ok "UNLOCKED in this session: $msg" }
      elseif ($ratio -le 0.65) { Bad "STOCK rate in this session: $msg"; Note 'If the PC slept since it started, that is expected: sleep returns the card to stock. Restart to unlock again.' }
      else { Bad "inconclusive (another GPU load running?): $msg" }
    }
  }
} catch {
  Bad "the check stopped: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))"
} finally {
  Write-Host "`n$(if ($script:failed) { "$($script:failed) check(s) FAILED" } else { 'All checks passed.' })"
  Stop-Transcript | Out-Null
}
exit $script:failed
