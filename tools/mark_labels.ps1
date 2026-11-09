# RoutePerApp - setzt uebersetzbare Beschriftungen in Tr(...) ein.
# Nur Zeilen mit bekannten UI-Stellen werden angefasst, damit Fensterklassen, Pfadnamen,
# Registrierungsschluessel und Protokolltexte unveraendert bleiben.
$files = "src/gui.cpp", "src/gui_logic.cpp", "src/gui_drag.cpp"

$ui = @(
    'AppendMenuW', 'InsertMenuW', 'ModifyMenuW', 'CheckMenuItem',
    'SetWindowTextW', 'SetWindowText', 'lpszWindow',
    'TaskDialog', 'MessageBoxW',
    'LVM_INSERTCOLUMNW', 'LVM_INSERTCOLUMN', 'LVM_SETITEMTEXTW', 'LVM_SETITEMTEXT',
    'LVITEM', 'item.pszText', 'item.szText', 'col.pszText', 'colitem',
    'TextOutW', 'TextOut', 'DrawTextW', 'ExtTextOutW', 'SetDlgItemTextW',
    'NIM_ADD', 'NIM_MODIFY', 'szTip',
    'SetStatus', 'Status', 'Notify', 'Balloon', 'AppendLine', 'AddLine'
)
$skip = @(
    'RegisterClass', 'lpszClassName', 'CardZone', 'RoutePerApp.', 'SysListView',
    'Local\\', 'Global\\', 'schtasks', '/TN ', '/Create', '/Delete', '/Query',
    '.json', '.exe', '.dll', '.log', '.bak', '.scr', 'Software\\', 'HKCU', 'HKLM',
    '--selbsttest', '/selbsttest', 'wcscmp', 'lstrcmp', 'ShellExecute',
    'CreateProcess', 'wcslwr', '_wcsicmp', 'class', 'Class'
)

foreach ($file in $files) {
    $lines = [System.IO.File]::ReadAllLines($file)
    $changed = 0
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $line = $lines[$i]
        if ($line -notmatch 'L"') { continue }
        if ($line -match 'Tr\(') { continue }
        $isUi = $false
        foreach ($u in $ui) { if ($line -match [regex]::Escape($u)) { $isUi = $true; break } }
        if (-not $isUi) { continue }
        $blocked = $false
        foreach ($s in $skip) { if ($line -match [regex]::Escape($s)) { $blocked = $true; break } }
        if ($blocked) { continue }
        $new = [regex]::Replace($line, 'L"(?:[^"\\]|\\.)*"', { param($m) "Tr($($m.Value))" })
        if ($new -ne $line) { $lines[$i] = $new; $changed++ }
    }
    [System.IO.File]::WriteAllLines($file, $lines)
    Write-Output ("{0}: {1} Zeilen gesetzt" -f $file, $changed)
}
