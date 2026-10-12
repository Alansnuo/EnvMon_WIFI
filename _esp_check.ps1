# ESP8266 + 烧录器 全面体检: 固件版本 / 模式 / 连热点 / MQTT 全套
# 参数直接从 APP/app_config.h 读, 保证和固件用的一致
$ErrorActionPreference = 'Continue'
$CRLF   = [string][char]13 + [string][char]10
$LATIN1 = [System.Text.Encoding]::GetEncoding(28591)
$UTF8   = [System.Text.Encoding]::UTF8

$cfg = Get-Content -Raw -Encoding UTF8 'E:\EnvMon_WIFI\APP\app_config.h'
function Cfg($n) { [regex]::Match($cfg, $n + '\s+"([^"]*)"').Groups[1].Value }
function CfgN($n) { [regex]::Match($cfg, $n + '\s+(\d+)').Groups[1].Value }
$ssid = Cfg 'CFG_WIFI_SSID'; $pw = Cfg 'CFG_WIFI_PASSWORD'
$host_ = Cfg 'CFG_MQTT_HOST'; $cid = Cfg 'CFG_MQTT_CLIENT_ID'; $user = Cfg 'CFG_MQTT_USERNAME'
$token = Cfg 'CFG_MQTT_PASSWORD'; $topic = Cfg 'CFG_MQTT_PUB_TOPIC'
$port = CfgN 'CFG_MQTT_PORT'
$ssidRaw = $LATIN1.GetString($UTF8.GetBytes($ssid))

Write-Host ("配置: SSID=[" + $ssid + "]  密码长度=" + $pw.Length + "  HOST=" + $host_ + ":" + $port)
Write-Host ("      CLIENTID=" + $cid + "  USERNAME=" + $user)
Write-Host ""

$sp = New-Object System.IO.Ports.SerialPort
$sp.PortName='COM9'; $sp.BaudRate=115200; $sp.Parity='None'; $sp.DataBits=8; $sp.StopBits='One'
$sp.Encoding = $LATIN1
$sp.ReadTimeout = 1500; $sp.WriteTimeout = 3000
$ok = $false
for($i=1; $i -le 6; $i++){
  try { $sp.Open(); $ok = $true; break }
  catch { Write-Host ("   COM9 打不开(第 $i 次): " + $_.Exception.Message); Start-Sleep -Milliseconds 1000 }
}
if(-not $ok){ Write-Host "COM9 一直被占用 —— 先关掉串口助手/烧写工具"; exit 1 }

# ⚠️ 这类 ESP8266 烧录器把 DTR/RTS 接到了 RST/GPIO0, **一开串口模块就被复位**,
#    而 ESP8266 复位后要约 1~2 秒才跑起 AT 固件。等太短的话后面每条命令都超时,
#    看起来像"模块坏了", 其实只是没就绪。
Start-Sleep -Milliseconds 2000
$sp.DiscardInBuffer(); $sp.DiscardOutBuffer()

$ready = $false
for($i=0; $i -lt 12; $i++){
  $sp.Write("AT" + $CRLF)
  Start-Sleep -Milliseconds 500
  if($sp.BytesToRead -gt 0){
    $t = $sp.ReadExisting()
    if($t -match 'OK'){ $ready = $true; break }
  }
}
if($ready){ Write-Host "   模块就绪 (AT -> OK)" } else { Write-Host "   ⚠️ 等了 6 秒 AT 还是没回 OK" }
$sp.DiscardInBuffer(); $sp.DiscardOutBuffer()

function Dec([string]$t){
    if([string]::IsNullOrEmpty($t)){ return '(空)' }
    $u = $UTF8.GetString($LATIN1.GetBytes($t)).Replace([string][char]13,'').Replace([string][char]10,'|')
    if($u.Length -gt 700){ $u = $u.Substring(0,700)+' ...' }
    return $u
}
function ReadFor([int]$ms){
    $acc=''; $end=(Get-Date).AddMilliseconds($ms)
    while((Get-Date) -lt $end){
        Start-Sleep -Milliseconds 250
        if($sp.BytesToRead -gt 0){ $acc += $sp.ReadExisting() }
    }
    return $acc
}
function Cmd([string]$c, [int]$ms=1200, [string]$echoAs=''){
    if(-not $sp.IsOpen){ return '' }
    $sp.Write($c + $CRLF)
    $r = ReadFor $ms
    $shown = if($echoAs){$echoAs}else{$c}
    if($shown.Length -gt 120){ $shown = $shown.Substring(0,120)+' ...' }
    Write-Host ("   >> " + $shown)
    Write-Host ("      <= " + (Dec $r))
    return $r
}

Write-Host "========== 1. 模块身份 + 固件版本 =========="
Cmd 'AT' 900 | Out-Null
Cmd 'ATE0' 900 | Out-Null
Cmd 'AT+GMR' 1500 | Out-Null

Write-Host ""
Write-Host "========== 2. 模式 / 自动重连 =========="
Cmd 'AT+CWMODE?' 900 | Out-Null
Cmd 'AT+CWAUTOCONN=0' 900 | Out-Null
Cmd 'AT+CIPSTAMAC?' 900 | Out-Null

Write-Host ""
Write-Host "========== 3. 能不能扫到你的热点 =========="
Cmd 'AT+CWLAP' 7000 | Out-Null

Write-Host ""
Write-Host "========== 4. 连热点 =========="
Cmd 'AT+CWQAP' 1500 | Out-Null
Cmd ('AT+CWJAP="' + $ssidRaw + '","' + $pw + '"') 15000 'AT+CWJAP="<SSID>","<PASSWORD>"' | Out-Null
Cmd 'AT+CIPSTA?' 1500 | Out-Null

Write-Host ""
Write-Host "========== 5. MQTT 有没有这套指令 =========="
Cmd 'AT+MQTTUSERCFG=?' 900 | Out-Null

Write-Host ""
Write-Host "========== 6. MQTT 全套 (OneNET) =========="
Cmd ('AT+MQTTUSERCFG=0,1,"' + $cid + '","' + $user + '","",0,0,""') 1500 | Out-Null
Cmd ('AT+MQTTPASSWORD=0,"' + $token + '"') 2000 'AT+MQTTPASSWORD=0,"<TOKEN>"' | Out-Null
Cmd 'AT+MQTTCONNCFG=0,60,0,"","",0,0' 1500 | Out-Null
Cmd ('AT+MQTTCONN=0,"' + $host_ + '",' + $port + ',0') 20000 | Out-Null
Cmd 'AT+MQTTCONN?' 1500 | Out-Null
Cmd ('AT+MQTTPUB=0,"' + $topic + '","{\"id\":\"1\"\,\"version\":\"1.0\"\,\"params\":{\"Temp\":{\"value\":25.0}}}",0,0') 6000 | Out-Null

$sp.Close()
Write-Host ""
Write-Host "DONE"
