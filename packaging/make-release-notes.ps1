# =============================================================================
# make-release-notes.ps1 - Phase 14: auto-generates GitHub release notes for
# the Windows port.
#
# Produces a Markdown body with:
#   * version + commit SHA + date
#   * upstream revision pins (parsed from NOTICE-WINDOWS-PORT.md)
#   * Windows-specific changes (curated summary; full detail lives in
#     docs/upstream-porting-notes.md and docs/implementation-roadmap.md)
#   * limitations (NOT POSSIBLE rows parsed from docs/windows-port-parity.md)
#   * hardware notes (CI reality statement - brief section 64: never claim
#     hardware testing that did not happen)
#   * attribution + license pointer
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File packaging/make-release-notes.ps1 `
#       -Version 6.0.0-w1 -Sha <commit-sha> -OutPath release-notes.md
#
# NOTE: keep this file ASCII-only. PowerShell 5.1 reads BOM-less .ps1 as
# ANSI; any non-ASCII byte (e.g. a UTF-8 em dash) mangles the parser.
# =============================================================================
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Sha   = "",
    [string]$OutPath = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# ---------------------------------------------------------------------------
# Upstream pins from NOTICE-WINDOWS-PORT.md (lines like:
#   * [gpu-screen-recorder](https://.../) 6.0.0 (r1467 / d31b698)
# ---------------------------------------------------------------------------
$upstreamLines = @()
# -Encoding UTF8: the docs are UTF-8; PS 5.1 defaults to ANSI for BOM-less
# files and would mangle non-ASCII (em dashes in the parity notes).
foreach ($line in (Get-Content -Encoding UTF8 (Join-Path $RepoRoot "NOTICE-WINDOWS-PORT.md"))) {
    if ($line -match '^\* \[([^\]]+)\]\([^)]+\)\s+([0-9.]+)\s+\(r(\d+) / ([0-9a-f]+)\)') {
        $upstreamLines += "- $($Matches[1]) $($Matches[2]) (revision r$($Matches[3]) / $($Matches[4]))"
    }
}
if ($upstreamLines.Count -eq 0) {
    throw "could not parse upstream pins from NOTICE-WINDOWS-PORT.md"
}

# ---------------------------------------------------------------------------
# Limitations: NOT POSSIBLE rows from docs/windows-port-parity.md. Each row
# is a table line whose Status column is "NOT POSSIBLE".
# ---------------------------------------------------------------------------
$notPossible = @()
foreach ($line in (Get-Content -Encoding UTF8 (Join-Path $RepoRoot "docs/windows-port-parity.md"))) {
    # Table columns: Feature | Linux | Windows | Status | Notes. Split on |
    # and trim; index 0 is the empty lead, so Status is index 4, Notes 5.
    $cols = $line -split '\|' | ForEach-Object { $_.Trim() }
    if ($cols.Count -ge 6 -and $cols[4] -eq "NOT POSSIBLE") {
        $notPossible += "- **$($cols[1])**: $($cols[5])"
    }
}

$date = (Get-Date).ToString("yyyy-MM-dd")

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# GPU Screen Recorder for Windows - v$Version")
[void]$sb.AppendLine()
[void]$sb.AppendLine("Windows port of [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/) by dec05eba. Build: $date" + $(if ($Sha) { " (commit $Sha)" } else { "" }))
[void]$sb.AppendLine()
[void]$sb.AppendLine("## Upstream revisions")
[void]$sb.AppendLine()
foreach ($l in $upstreamLines) { [void]$sb.AppendLine($l) }
[void]$sb.AppendLine()
[void]$sb.AppendLine("## What's new in this port")
[void]$sb.AppendLine()
[void]$sb.AppendLine("This release ships the full Windows port pipeline: the engine (Windows Graphics Capture primary, DXGI Desktop Duplication fallback, ANGLE GL render backend), NVIDIA NVENC hardware encoding with capability probing, WASAPI audio capture, replay buffer (RAM + disk), the mgl-based UI with the original branding, engine IPC + gsr-cli, portable autostart, and a complete installer + portable ZIP.")
[void]$sb.AppendLine()
[void]$sb.AppendLine("Detailed per-component notes: [docs/upstream-porting-notes.md](../docs/upstream-porting-notes.md), phase history: [docs/implementation-roadmap.md](../docs/implementation-roadmap.md).")
[void]$sb.AppendLine()
[void]$sb.AppendLine("## Known limitations")
[void]$sb.AppendLine()
[void]$sb.AppendLine("Features without a faithful Windows equivalent are hidden with a clear error, never faked (brief section 57):")
[void]$sb.AppendLine()
foreach ($np in $notPossible) { [void]$sb.AppendLine($np) }
[void]$sb.AppendLine()
[void]$sb.AppendLine("## Hardware notes")
[void]$sb.AppendLine()
[void]$sb.AppendLine("CI validates compilation, unit/integration tests, packaging and the pure-logic paths on GitHub-hosted runners. CI hardware has no guaranteed physical GPU, so NVENC encode quality, WGC against a real desktop, multi-monitor, HDR and high-refresh capture require validation on a physical NVIDIA GPU (see [docs/implementation-roadmap.md](../docs/implementation-roadmap.md) section 'CI hardware reality'). The port targets NVIDIA GPUs (NVENC required for hardware encoding); software encoding (libx264) remains available.")
[void]$sb.AppendLine()
[void]$sb.AppendLine("## License and attribution")
[void]$sb.AppendLine()
[void]$sb.AppendLine("GPL-3.0-only, as required by upstream. Full third-party attribution: [docs/licensing.md](../docs/licensing.md). This port is a modified version of the upstream GPL-3.0-only projects listed above; all upstream copyrights remain with their authors.")

$body = $sb.ToString()

if ($OutPath) {
    # No BOM via .NET: PS 5.1's Set-Content has no UTF8NoBOM enum, and a BOM
    # would show as a stray character at the top of the release body.
    [System.IO.File]::WriteAllText($OutPath, $body, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "== wrote $OutPath ($($body.Length) chars)"
} else {
    $body
}
