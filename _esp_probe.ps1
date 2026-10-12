# 分离实验: 先用公共 broker(无鉴权) 验证模块的 MQTT 栈, 再回来试 OneNET
$ErrorActionPreference = 'Continue'
$CRLF   = [string][char]13 + [string][char]10
$LATIN1 = [System.Text.Encoding]::GetEncoding(28591)
$UTF8   = [System.Text.Encoding]::UTF8

$cfg = Get-Content -Raw -Encoding UTF8 'E:\EnvMon_WIFI\APP\app_config.h'
function Cfg($n) { [regex]::Match($cfg, $n + '\s+"([^"]*)"').Groups[1].Value }
function CfgN($n) { [regex]::Match($cfg, $n + '\s+(\d+)').Groups[1].Value }
$ssid = Cfg 'CFG_WIFI_SSID'; $pw = Cfg 'CFG_WIFI_PASSWORD'
$host_ = Cfg 'CFG_MQTT_HOST'; $cid = Cfg 'CFG_MQTT_CLIENT_ID'; $user = Cfg 'CFG_MQTT_USERNAME'
$token = Cfg 'CFG_MQTT_PASSWORD'; $topic = Cfg 'CFG_MQTT_PUB_TOPIC'; $ka = CfgN 'CFG_MQTT_KEEPALIVE_S'
$port = CfgN 'CFG_MQTT_PORT'
$ssidRaw = $LATIN1.GetString($UTF8.GetBytes($ssid))

$sp = New-Object System.IO.Ports.SerialPort
$sp.PortName='COM9'; $sp.BaudRate=115200; $sp.Parity='None'; $sp.DataBits=8; $sp.StopBits='One'
$sp.Encoding = $LATIN1
$sp.ReadTimeout = 1500; $sp.WriteTimeout = 3000
$ok=$false
for($i=1;$i -le 6;$i++){ try{ $sp.Open(); $ok=$true; break }catch{ Start-Sleep -Milliseconds 1000 } }
if(-not $ok){ throw 'COM9 打不开' }
Start-Sleep -Milliseconds 400
$sp.DiscardInBuffer(); $sp.DiscardOutBuffer()

function Dec([string]$t){
    if([string]::IsNullOrEmpty($t)){ return '' }
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
function Cmd([string]$c, [int]$ms=1500, [string]$echoAs=''){
    $sp.Write($c + $CRLF)
    $r = ReadFor $ms
    $shown = if($echoAs){$echoAs}else{$c}
    if($shown.Length -gt 120){ $shown = $shown.Substring(0,120)+' ...' }
    Write-Host ("   >> " + $shown)
    Write-Host ("      <= " + (Dec $r))
    return $r
}

Write-Host "========== 准备: 连 WiFi =========="
Cmd 'AT' 800 | Out-Null
Cmd 'ATE0' 800 | Out-Null
Cmd 'AT+CWMODE=1' 800 | Out-Null
Cmd 'AT+CWAUTOCONN=0' 800 | Out-Null
Cmd 'AT+CWQAP' 1500 | Out-Null
Cmd ('AT+CWJAP="'+$ssidRaw+'","'+$pw+'"') 15000 'AT+CWJAP="<SSID>","<PASSWORD>"' | Out-Null

Write-Host ""
Write-Host "========== 实验 A: 公共 broker broker.emqx.io:1883 (无鉴权) =========="
Write-Host "   如果这个能连上 => 模块 MQTT 栈没问题, 问题在 OneNET 凭据"
Cmd 'AT+MQTTUSERCFG=0,1,"esptest001","","",0,0,""' 2000 | Out-Null
Cmd 'AT+MQTTCONNCFG=0,60,0,"","",0,0' 2000 | Out-Null
Cmd 'AT+MQTTCONN=0,"broker.emqx.io",1883,0' 25000 | Out-Null
Cmd 'AT+MQTTCONN?' 2000 | Out-Null
Write-Host "   试发一条到公共 broker 的 topic:"
Cmd 'AT+MQTTPUB=0,"esptest001/t","hello",0,0' 8000 | Out-Null
Cmd 'AT+MQTTDISCONN=0' 3000 | Out-Null

Write-Host ""
Write-Host "========== 实验 B: 再试 OneNET (换一个新 ClientID 排除挤下线) =========="
Cmd ('AT+MQTTUSERCFG=0,1,"'+$cid+'","'+$user+'","",0,0,""') 2000 | Out-Null
Cmd ('AT+MQTTPASSWORD=0,"'+$token+'"') 2500 'AT+MQTTPASSWORD=0,"<TOKEN>"' | Out-Null
Cmd ('AT+MQTTCONNCFG=0,'+$ka+',0,"","",0,0') 2000 | Out-Null
Cmd ('AT+MQTTCONN=0,"'+$host_+'",'+$port+',0') 25000 | Out-Null
Cmd 'AT+MQTTCONN?' 2000 | Out-Null
Cmd ('AT+MQTTPUB=0,"'+$topic+'","{\"id\":\"1\"\,\"version\":\"1.0\"\,\"params\":{\"Temp\":{\"value\":25.0}}}",0,0') 8000 | Out-Null

$sp.Close()
Write-Host ""
Write-Host "DONE"
