# COM9 收不到任何东西时的排查: DTR/RTS 组合 + 两种波特率 + bootloader 探测
$ErrorActionPreference = 'Continue'
$CRLF   = [string][char]13 + [string][char]10
$LATIN1 = [System.Text.Encoding]::GetEncoding(28591)
$UTF8   = [System.Text.Encoding]::UTF8

function TryCombo([bool]$dtr, [bool]$rts, [int]$baud) {
    $sp = New-Object System.IO.Ports.SerialPort
    $sp.PortName='COM9'; $sp.BaudRate=$baud; $sp.Parity='None'; $sp.DataBits=8; $sp.StopBits='One'
    $sp.Encoding = $LATIN1
    $sp.ReadTimeout = 1500; $sp.WriteTimeout = 1500
    try { $sp.Open() } catch { Write-Host ("   open fail: " + $_.Exception.Message); return }
    try {
        $sp.DtrEnable = $dtr
        $sp.RtsEnable = $rts
        Start-Sleep -Milliseconds 250
        $sp.DiscardInBuffer(); $sp.DiscardOutBuffer()

        # 被动听
        Start-Sleep -Milliseconds 800
        $n0 = $sp.BytesToRead; if($n0 -gt 0){ $sp.ReadExisting() | Out-Null }

        # 发 AT
        $sp.Write('AT' + $CRLF); Start-Sleep -Milliseconds 500
        $n1 = $sp.BytesToRead; $r1 = ''
        if($n1 -gt 0){ $r1 = $sp.ReadExisting() }

        # 发 0xC0
        $p = New-Object byte[] 4; for($i=0;$i -lt 4;$i++){ $p[$i]=0xC0 }
        $sp.Write($p,0,4); Start-Sleep -Milliseconds 700
        $n2 = $sp.BytesToRead; if($n2 -gt 0){ $sp.ReadExisting() | Out-Null }

        $t = $r1.Replace([string][char]13,'').Replace([string][char]10,'|')
        Write-Host ("   DTR={0,-5} RTS={1,-5} baud={2,-7} listen={3,-4} AT={4,-4} sync={5,-4}  [{6}]" -f $dtr,$rts,$baud,$n0,$n1,$n2,$t)
    } finally { try { $sp.Close() } catch {} }
}

Write-Host "=== DTR/RTS x 波特率 扫描 ==="
foreach($b in 115200,74880){
  foreach($d in $false,$true){
    foreach($r in $false,$true){ TryCombo $d $r $b }
  }
}
Write-Host ""
Write-Host "判读:  AT>0 或 sync>0 = 模块有反应;  全 0 = 模块没插好/没供电/被按住复位"
