param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [Parameter(Mandatory = $true)][string]$ReplayFile,
    [switch]$LeaveOpen
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$binary = Join-Path $root "Run64\$Configuration\DummyClient.exe"
$recording = (Resolve-Path -LiteralPath $ReplayFile).Path
$outputDirectory = Join-Path $root '.deps\replay-window-check'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Text;
using System.Drawing;
using System.Runtime.InteropServices;
public static class ReplayUi {
    public delegate bool EnumCallback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder text, int max);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr WriteText(IntPtr window, int message, IntPtr unused, string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr window, int message, IntPtr count, StringBuilder text);
    public static IntPtr Find(int processId) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((w, d) => { uint pid; GetWindowThreadProcessId(w, out pid);
            var text = new StringBuilder(256); GetClassName(w, text, 256);
            if(pid == processId && text.ToString() == "PortfolioReplayWindow") { result = w; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static string Read(IntPtr window, int id) {
        var text = new StringBuilder(512); ReadText(GetDlgItem(window,id), 0xD, (IntPtr)512, text); return text.ToString();
    }
}
'@

function Send-Control([int]$Id) { $null = [ReplayUi]::SendMessage($script:viewer, 0x111, [IntPtr]$Id, [IntPtr]::Zero) }
function Read-Time {
    $text = [ReplayUi]::Read($script:viewer, 107)
    if ($text -notmatch '(PLAYING|PAUSED) \| ([\d.]+)x \| ([\d.]+) / ([\d.]+) s') { throw "Missing status: $text" }
    return [PSCustomObject]@{ State=$Matches[1]; Speed=[double]$Matches[2]; Time=[double]$Matches[3]; Duration=[double]$Matches[4] }
}
function Set-Speed([string]$Value) {
    $null = [ReplayUi]::WriteText([ReplayUi]::GetDlgItem($script:viewer, 103), 0xC, [IntPtr]::Zero, $Value)
    Send-Control 104
}
function Seek-Fraction([int]$Value) {
    $slider = [ReplayUi]::GetDlgItem($script:viewer,105)
    $null = [ReplayUi]::SendMessage($script:viewer,0x114,[IntPtr]5,$slider)
    $null = [ReplayUi]::SendMessage($slider,0x405,[IntPtr]1,[IntPtr]$Value)
    $null = [ReplayUi]::SendMessage($script:viewer,0x114,[IntPtr]8,$slider)
}

# This test controls only the viewer process it starts. No servers or DB are needed.
$windowStyle = if ($LeaveOpen) { 'Normal' } else { 'Hidden' }
$process = Start-Process -FilePath $binary -ArgumentList @('--replay',('"'+$recording+'"')) -WorkingDirectory $root -WindowStyle $windowStyle -PassThru
$passed = $false
try {
    $script:viewer = [IntPtr]::Zero
    for ($i=0; $i -lt 1200 -and $script:viewer -eq [IntPtr]::Zero; ++$i) {
        if ($process.HasExited) { throw "Viewer exited: $($process.ExitCode)" }
        Start-Sleep -Milliseconds 100
        $script:viewer = [ReplayUi]::Find($process.Id)
    }
    if ($script:viewer -eq [IntPtr]::Zero) { throw 'Viewer did not open' }
    $initial = Read-Time
    if ($initial.State -ne 'PAUSED' -or $initial.Speed -ne 1 -or $initial.Duration -lt 15) { throw 'Need a >=15s recording; expected initial paused 1x' }
    Send-Control 101; Start-Sleep -Milliseconds 1200; Send-Control 102
    $normal = Read-Time; $advance = $normal.Time - $initial.Time
    if ($advance -lt 0.8 -or $advance -gt 2.5) { throw "1x playback delta=$advance" }
    Start-Sleep -Milliseconds 250
    if ((Read-Time).Time -ne $normal.Time) { throw 'Pause did not freeze time' }
    $null = [ReplayUi]::SendMessage([ReplayUi]::GetDlgItem($script:viewer,103),0x14E,[IntPtr]4,[IntPtr]::Zero)
    $null = [ReplayUi]::SendMessage($script:viewer,0x111,[IntPtr](65536+103),[IntPtr]::Zero)
    Send-Control 101; Start-Sleep -Milliseconds 800; Send-Control 102
    $fast = Read-Time; $advance = $fast.Time - $normal.Time
    if ($fast.Speed -ne 4 -or $advance -lt 2.5 -or $advance -gt 5.5) { throw "4x playback delta=$advance" }
    Send-Control 101
    if ((Read-Time).Speed -ne 4) { throw 'Resume reset the selected speed' }
    Set-Speed '2'; $before = Read-Time; Start-Sleep -Milliseconds 800; Send-Control 102
    $changed = Read-Time; $advance = $changed.Time - $before.Time
    if ($changed.Speed -ne 2 -or $advance -lt 1.1 -or $advance -gt 3) { throw "Live speed change delta=$advance" }
    Seek-Fraction 5000; $middle = Read-Time
    if ([Math]::Abs($middle.Time - $middle.Duration/2) -gt 0.1) { throw 'Seek did not reach midpoint' }
    Seek-Fraction 10000
    $end = Read-Time
    if ($end.State -ne 'PAUSED' -or $end.Time -ne $end.Duration) { throw 'End should be paused at duration' }
    Send-Control 101; Send-Control 102
    $rewound = Read-Time
    if ($rewound.Time -gt 1 -or $rewound.Speed -ne 2) { throw "End replay did not rewind with retained speed: $rewound" }
    Set-Speed '1'; Send-Control 106; Send-Control 101; Start-Sleep -Milliseconds 2000; Send-Control 102
    $screenshot = Join-Path $outputDirectory "$Configuration.png"
    $position = Read-Time
    & $binary --replay $recording --image $screenshot --at $position.Time | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Replay frame export failed' }
    $bitmap = [Drawing.Bitmap]::FromFile($screenshot)
    try {
        if ($bitmap.Width -lt 1000 -or $bitmap.GetPixel(10,10).R -eq 0) { throw 'Replay image is empty' }
    } finally { $bitmap.Dispose() }
    Write-Output "[OK] $Configuration graphical replay: default 1x, continuous play, pause, resume speed, live speed, seek, replay after end."
    Write-Output "[FRAME IMAGE] $screenshot"
    Write-Output "[VIEWER] pid=$($process.Id) hwnd=$script:viewer"
    $passed = $true
}
finally {
    if (-not ($LeaveOpen -and $passed)) {
        if ($script:viewer -ne [IntPtr]::Zero) { $null = [ReplayUi]::SendMessage($script:viewer,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
        if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
    }
    $process.Dispose()
}
