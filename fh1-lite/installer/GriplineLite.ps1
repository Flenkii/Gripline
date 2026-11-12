# Gripline Lite installer for Forza Horizon 1: ReImagined
# Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
#
# Copies the Gripline Lite source files into the ReImagined launcher's project folder and adds
# them to its build (CMakeLists.txt, src/kernel/input_autopress.cpp). Every line it adds ends with
# "Gripline Lite", so removing takes exactly those lines out again. Originals are backed up first.
# After installing, the launcher's Setup > Build > Rebuild compiles the game with Gripline Lite.
# Nothing is downloaded and no game file is changed.

param([switch]$NoGui, [string]$Action = "", [string]$Project = "")

$ErrorActionPreference = "Stop"
$Version = "0.9"
$Marker = "Gripline Lite"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Files = Join-Path (Split-Path -Parent $Here) "src\hooks"
$HookFiles = @("gripline_settings.h", "gripline_wheel.cpp", "gripline_dashcam.cpp",
               "gripline_menu.cpp", "gripline_traffic.cpp")
$German = (Get-UICulture).TwoLetterISOLanguageName -eq "de"

function T([string]$de, [string]$en) { if ($German) { $de } else { $en } }

function Default-Project {
  Join-Path $env:LOCALAPPDATA "Programs\ForzaHorizon1-ReImagined\project\forza-recomp"
}

function Launcher-Exe {
  Join-Path $env:LOCALAPPDATA "Programs\ForzaHorizon1-ReImagined\forza-launcher.exe"
}

function Paths([string]$proj) {
  @{
    CMake = Join-Path $proj "CMakeLists.txt"
    Input = Join-Path $proj "src\kernel\input_autopress.cpp"
    Hooks = Join-Path $proj "src\hooks"
    Stamp = Join-Path $proj "gripline-lite.version"
    Backup = Join-Path $proj "gripline-lite-backup"
  }
}

function Valid-Project([string]$proj) {
  $p = Paths $proj
  (Test-Path $p.CMake) -and (Test-Path $p.Input) -and (Test-Path (Join-Path $p.Hooks "camera_fov.cpp"))
}

function Read-Lines([string]$path) {
  $text = [IO.File]::ReadAllText($path)
  $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
  $lines = [Collections.Generic.List[string]]::new()
  $lines.AddRange([string[]]($text -split "`r?`n", -1))
  @{ Lines = $lines; NewLine = $nl }
}

function Write-Lines([string]$path, $doc) {
  $text = [string]::Join($doc.NewLine, $doc.Lines)
  [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false))
}

# The one line that matches $pattern (a regex); throws when there is none or more than one.
function Find-One($lines, [string]$pattern, [string]$what) {
  $found = @()
  for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match $pattern) { $found += $i } }
  if ($found.Count -ne 1) {
    throw (T "Diese ReImagined-Version wird nicht unterstuetzt (Stelle '$what' nicht gefunden)." `
             "This ReImagined version is not supported (could not find '$what').")
  }
  $found[0]
}

function Remove-Patch($lines, [string]$kind) {
  # Every Gripline line, also unmarked ones from hand-made test builds: lines that use
  # forza::gripline, and a multi-line "if (forza::gripline::...) {" block up to its "}".
  $drop = New-Object 'System.Collections.Generic.HashSet[int]'
  $block = $false
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i]
    if ($kind -eq "cmake") {
      if ($l -match "src/hooks/gripline_") { [void]$drop.Add($i) }
      continue
    }
    if ($block) {
      [void]$drop.Add($i)
      if ($l.Trim() -eq "}") { $block = $false }
      continue
    }
    if ($l.Contains($Marker) -or $l.Contains("forza::gripline::") -or $l.Contains("gripline_settings.h") -or $l.Contains("gripline_caps")) {
      [void]$drop.Add($i)
      if ($l.Contains("forza::gripline::") -and $l.TrimEnd().EndsWith("{")) { $block = $true }
    }
  }
  for ($i = $lines.Count - 1; $i -ge 0; $i--) {
    if ($drop.Contains($i)) { $lines.RemoveAt($i) }
  }
}

function Get-State([string]$proj) {
  if (-not (Valid-Project $proj)) { return "noproject" }
  $p = Paths $proj
  $cmake = [IO.File]::ReadAllText($p.CMake).Contains("src/hooks/gripline_")
  $inp = [IO.File]::ReadAllText($p.Input).Contains($Marker)
  $files = -not ($HookFiles | Where-Object { -not (Test-Path (Join-Path $p.Hooks $_)) })
  $stamp = if (Test-Path $p.Stamp) { (Get-Content $p.Stamp -Raw).Trim() } else { "" }
  if ($cmake -and $inp -and $files -and $stamp) { return "installed:$stamp" }
  if ($stamp -or $cmake -or $inp) { return "broken" }
  "none"
}

function Install([string]$proj) {
  if (-not (Valid-Project $proj)) { throw (T "Das ist kein ReImagined-Projektordner." "This is not a ReImagined project folder.") }
  foreach ($f in $HookFiles) {
    if (-not (Test-Path (Join-Path $Files $f))) { throw "Missing $f next to the installer." }
  }
  $p = Paths $proj
  # Backup of the files we change, once per install.
  $dir = Join-Path $p.Backup (Get-Date -Format "yyyy-MM-dd_HH-mm-ss")
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  Copy-Item $p.CMake $dir
  Copy-Item $p.Input $dir

  $cmake = Read-Lines $p.CMake
  Remove-Patch $cmake.Lines "cmake"
  $at = Find-One $cmake.Lines '^\s*src/hooks/camera_fov\.cpp\s*$' "src/hooks/camera_fov.cpp"
  $indent = ($cmake.Lines[$at] -replace '\S.*$', '')
  $add = @("gripline_wheel.cpp", "gripline_dashcam.cpp", "gripline_menu.cpp", "gripline_traffic.cpp") |
         ForEach-Object { "${indent}src/hooks/$_" }
  $cmake.Lines.InsertRange($at + 1, [string[]]$add)

  $in = Read-Lines $p.Input
  Remove-Patch $in.Lines "input"
  $l = $in.Lines
  $i = Find-One $l '^#include "\.\./platform/wheel_bridge\.h"' "wheel_bridge.h"
  $l.Insert($i + 1, '#include "../hooks/gripline_settings.h"  // Gripline Lite')
  $i = Find-One $l 'state\.buttons = forza::CustomRacePad\(state\.buttons\);' "CustomRacePad"
  $l.InsertRange($i + 1, [string[]]@(
    '    if (!block_all) state.buttons = forza::gripline::DashCamPad(base, state.buttons);  // Gripline Lite',
    '    if (forza::gripline::MenuOpen()) state.buttons = 0, state.sticks[2] = state.sticks[3] = 0;  // Gripline Lite'))
  $i = Find-One $l 'Capabilities\(sdk, ctx, base, forza::input_caps::CapabilitiesArgs\(ctx\)\);' "CapabilitiesArgs"
  $l.Insert($i + 1, '  forza::gripline::NoteCaps(base, ctx.r3.u32, gripline_caps);  // Gripline Lite')
  $l.Insert($i, '  const uint32_t gripline_caps = ctx.r5.u32;  // Gripline Lite')
  $i = Find-One $l 'Capabilities\(sdk, ctx, base, forza::input_caps::CapabilitiesExArgs\(ctx\)\);' "CapabilitiesExArgs"
  $l.Insert($i + 1, '  forza::gripline::NoteCaps(base, ctx.r3.u32, gripline_caps);  // Gripline Lite')
  $l.Insert($i, '  const uint32_t gripline_caps = ctx.r6.u32;  // Gripline Lite')

  # All anchors found: write everything.
  foreach ($f in $HookFiles) { Copy-Item (Join-Path $Files $f) (Join-Path $p.Hooks $f) -Force }
  Write-Lines $p.CMake $cmake
  Write-Lines $p.Input $in
  Set-Content -Path $p.Stamp -Value $Version -Encoding ASCII
}

function Uninstall([string]$proj) {
  if (-not (Valid-Project $proj)) { throw (T "Das ist kein ReImagined-Projektordner." "This is not a ReImagined project folder.") }
  $p = Paths $proj
  $cmake = Read-Lines $p.CMake
  Remove-Patch $cmake.Lines "cmake"
  Write-Lines $p.CMake $cmake
  $in = Read-Lines $p.Input
  Remove-Patch $in.Lines "input"
  Write-Lines $p.Input $in
  foreach ($f in $HookFiles) { Remove-Item (Join-Path $p.Hooks $f) -ErrorAction SilentlyContinue }
  Remove-Item $p.Stamp -ErrorAction SilentlyContinue
}

function State-Text([string]$state) {
  switch -Wildcard ($state) {
    "noproject" { T "ReImagined nicht gefunden. Bitte den Ordner 'project\forza-recomp' waehlen." "ReImagined not found. Please choose the 'project\forza-recomp' folder." }
    "none"      { T "Gripline Lite ist nicht installiert." "Gripline Lite is not installed." }
    "broken"    { T "Gripline Lite wurde durch ein ReImagined-Update entfernt. Bitte neu installieren." "A ReImagined update removed Gripline Lite. Please install it again." }
    "installed:*" {
      $v = $state.Substring(10)
      if ($v -eq $Version) { T "Gripline Lite $v ist installiert." "Gripline Lite $v is installed." }
      else { T "Gripline Lite $v ist installiert. Update auf $Version moeglich." "Gripline Lite $v is installed. Update to $Version available." }
    }
  }
}

# Command line use (also for tests): -NoGui -Action install|uninstall|state [-Project <folder>]
if ($NoGui) {
  $proj = if ($Project) { $Project } else { Default-Project }
  switch ($Action) {
    "install" { Install $proj; "installed" }
    "uninstall" { Uninstall $proj; "removed" }
    default { Get-State $proj }
  }
  return
}

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$bg = [Drawing.Color]::FromArgb(18, 20, 28)
$panel = [Drawing.Color]::FromArgb(30, 34, 46)
$accent = [Drawing.Color]::FromArgb(0, 150, 255)
$fg = [Drawing.Color]::FromArgb(235, 238, 245)
$muted = [Drawing.Color]::FromArgb(150, 158, 175)

$form = New-Object Windows.Forms.Form
$form.Text = "Gripline Lite $Version"
$form.ClientSize = New-Object Drawing.Size(560, 470)
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox = $false
$form.BackColor = $bg
$form.ForeColor = $fg
$form.Font = New-Object Drawing.Font("Segoe UI", 10)

$logoPath = Join-Path $Here "logo.png"
if (Test-Path $logoPath) {
  $logo = New-Object Windows.Forms.PictureBox
  $logo.Image = [Drawing.Image]::FromFile($logoPath)
  $logo.SizeMode = "Zoom"
  $logo.Location = New-Object Drawing.Point(24, 20)
  $logo.Size = New-Object Drawing.Size(96, 96)
  $form.Controls.Add($logo)
}

$title = New-Object Windows.Forms.Label
$title.Text = "GRIPLINE LITE $Version"
$title.Font = New-Object Drawing.Font("Segoe UI Semibold", 22)
$title.ForeColor = $fg
$title.AutoSize = $true
$title.Location = New-Object Drawing.Point(132, 28)
$form.Controls.Add($title)

$sub = New-Object Windows.Forms.Label
$sub.Text = T "für Forza Horizon 1: ReImagined" "for Forza Horizon 1: ReImagined"
$sub.ForeColor = $accent
$sub.AutoSize = $true
$sub.Location = New-Object Drawing.Point(136, 74)
$form.Controls.Add($sub)

$features = New-Object Windows.Forms.Label
$features.Text = T "Lenkrad 1:1 (900°)  •  Dashboard Cam  •  Hände ausblenden  •  Verkehr`nAlles live einstellbar im Spiel mit F6." `
                   "1:1 wheel (900°)  •  Dashboard Cam  •  Hide hands  •  Traffic`nEverything adjustable live in game with F6."
$features.ForeColor = $muted
$features.Location = New-Object Drawing.Point(24, 128)
$features.Size = New-Object Drawing.Size(512, 44)
$form.Controls.Add($features)

$box = New-Object Windows.Forms.Panel
$box.BackColor = $panel
$box.Location = New-Object Drawing.Point(24, 180)
$box.Size = New-Object Drawing.Size(512, 110)
$form.Controls.Add($box)

$folderLabel = New-Object Windows.Forms.Label
$folderLabel.ForeColor = $muted
$folderLabel.Location = New-Object Drawing.Point(12, 10)
$folderLabel.Size = New-Object Drawing.Size(488, 40)
$box.Controls.Add($folderLabel)

$status = New-Object Windows.Forms.Label
$status.Font = New-Object Drawing.Font("Segoe UI Semibold", 11)
$status.Location = New-Object Drawing.Point(12, 54)
$status.Size = New-Object Drawing.Size(488, 48)
$box.Controls.Add($status)

$hint = New-Object Windows.Forms.Label
$hint.ForeColor = $muted
$hint.Location = New-Object Drawing.Point(24, 300)
$hint.Size = New-Object Drawing.Size(512, 60)
$form.Controls.Add($hint)

function New-Button([string]$label, [int]$x, [int]$y, [int]$w, [bool]$primary) {
  $b = New-Object Windows.Forms.Button
  $b.Text = $label
  $b.FlatStyle = "Flat"
  $b.FlatAppearance.BorderSize = if ($primary) { 0 } else { 1 }
  $b.FlatAppearance.BorderColor = $muted
  $b.BackColor = if ($primary) { $accent } else { $bg }
  $b.ForeColor = if ($primary) { [Drawing.Color]::White } else { $fg }
  $b.Font = New-Object Drawing.Font("Segoe UI Semibold", 10)
  $b.Location = New-Object Drawing.Point($x, $y)
  $b.Size = New-Object Drawing.Size($w, 40)
  $form.Controls.Add($b)
  $b
}

$install = New-Button (T "Installieren" "Install") 24 370 180 $true
$remove = New-Button (T "Entfernen" "Remove") 212 370 120 $false
$launcher = New-Button (T "Launcher öffnen" "Open launcher") 340 370 196 $false
$folder = New-Button (T "Anderen Ordner wählen…" "Choose another folder…") 24 418 220 $false
$folder.Height = 32

$script:proj = Default-Project

function Refresh-View {
  $state = Get-State $script:proj
  $folderLabel.Text = (T "Projektordner: " "Project folder: ") + $script:proj
  $status.Text = State-Text $state
  $status.ForeColor = if ($state -like "installed:*") { [Drawing.Color]::FromArgb(90, 220, 140) }
                      elseif ($state -eq "none") { $fg } else { [Drawing.Color]::FromArgb(255, 180, 60) }
  $install.Text = if ($state -like "installed:*") { T "Neu installieren / Update" "Reinstall / update" } else { T "Installieren" "Install" }
  $install.Enabled = $state -ne "noproject"
  $remove.Enabled = $state -like "installed:*" -or $state -eq "broken"
  $hint.Text = T "Danach im ReImagined-Launcher: Setup › Build › Rebuild. Nach jedem ReImagined-Update diesen Installer noch einmal starten." `
                 "Then in the ReImagined launcher: Setup › Build › Rebuild. Run this installer again after every ReImagined update."
}

function Game-Running {
  [bool](Get-Process -Name "forza" -ErrorAction SilentlyContinue)
}

$install.Add_Click({
  try {
    Install $script:proj
    Refresh-View
    $msg = T "Gripline Lite $Version ist installiert.`n`nJetzt im ReImagined-Launcher: Setup › Build › Rebuild.`nIm Spiel öffnet F6 das Gripline-Menü." `
             "Gripline Lite $Version is installed.`n`nNow in the ReImagined launcher: Setup › Build › Rebuild.`nIn game, F6 opens the Gripline menu."
    if (Game-Running) { $msg += T "`n`nDas Spiel läuft noch: bitte vor dem Rebuild schließen." "`n`nThe game is still running: please close it before the rebuild." }
    [Windows.Forms.MessageBox]::Show($msg, "Gripline Lite", "OK", "Information") | Out-Null
  } catch {
    Refresh-View
    [Windows.Forms.MessageBox]::Show($_.Exception.Message, "Gripline Lite", "OK", "Error") | Out-Null
  }
})

$remove.Add_Click({
  $ok = [Windows.Forms.MessageBox]::Show((T "Gripline Lite entfernen?" "Remove Gripline Lite?"), "Gripline Lite", "YesNo", "Question")
  if ($ok -ne "Yes") { return }
  try {
    Uninstall $script:proj
    Refresh-View
    [Windows.Forms.MessageBox]::Show((T "Entfernt. Im Launcher noch einmal Rebuild drücken, dann ist das Spiel wieder original." `
                                         "Removed. Press Rebuild in the launcher once more to get the original game back."),
                                     "Gripline Lite", "OK", "Information") | Out-Null
  } catch {
    [Windows.Forms.MessageBox]::Show($_.Exception.Message, "Gripline Lite", "OK", "Error") | Out-Null
  }
})

$launcher.Add_Click({
  $exe = Launcher-Exe
  if (Test-Path $exe) { Start-Process $exe } else {
    [Windows.Forms.MessageBox]::Show((T "Launcher nicht gefunden: $exe" "Launcher not found: $exe"), "Gripline Lite") | Out-Null
  }
})

$folder.Add_Click({
  $dlg = New-Object Windows.Forms.FolderBrowserDialog
  $dlg.Description = T "Ordner 'project\forza-recomp' von ReImagined wählen" "Choose ReImagined's 'project\forza-recomp' folder"
  if ($dlg.ShowDialog() -eq "OK") { $script:proj = $dlg.SelectedPath; Refresh-View }
})

# Update hint: is there a newer Gripline Lite release on GitHub (tags fh1-lite-vX.Y)?
# Only reads the release list and opens the page in the browser; nothing is downloaded.
# Offline or any error: no hint.
$update = New-Object Windows.Forms.LinkLabel
$update.AutoSize = $true
$update.Location = New-Object Drawing.Point(260, 424)
$update.LinkColor = [Drawing.Color]::FromArgb(255, 180, 60)
$update.ActiveLinkColor = $fg
$update.Visible = $false
$form.Controls.Add($update)
$update.Add_LinkClicked({ Start-Process $update.Tag })

function Check-Update {
  try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/Flenkii/Gripline/releases?per_page=50" `
      -Headers @{ "User-Agent" = "GriplineLite-Installer" } -TimeoutSec 5
    $mine = [version]$Version
    $best = $null
    foreach ($r in $releases) {
      if ($r.draft -or $r.tag_name -notmatch '^fh1-lite-v(\d+(\.\d+){1,3})$') { continue }
      $v = [version]$Matches[1]
      if ($v -gt $mine -and (-not $best -or $v -gt $best.V)) { $best = @{ V = $v; Url = $r.html_url } }
    }
    if ($best) {
      $update.Text = T "Neue Version $($best.V) verfügbar – zur Download-Seite" "New version $($best.V) available – open download page"
      $update.Tag = $best.Url
      $update.Visible = $true
    }
  } catch { }
}

$form.Add_Shown({ $form.Refresh(); Check-Update })

Refresh-View
[void]$form.ShowDialog()
