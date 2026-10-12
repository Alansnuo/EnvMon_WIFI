$ErrorActionPreference = 'Stop'
$f   = 'E:\tianwen\报道温湿度_光照（串口0）.hd'
$bak = 'E:\tianwen\报道温湿度_光照（串口0）.hd.bak'
$utf8 = New-Object System.Text.UTF8Encoding($false)

if (-not (Test-Path $f)) { throw "找不到 $f" }
if (-not (Test-Path $bak)) { Copy-Item $f $bak -Force; Write-Output "BACKUP -> $bak" }
else { Write-Output "BACKUP 已存在, 保留: $bak" }

# 整份 XML 写成一行(和原文件一样), 放在单引号 here-string 里避免转义问题
$xml = @'
<xml xmlns="http://www.w3.org/1999/xhtml"><block type="setup" id="a01" x="0" y="0"><statement name="DO"><block type="block_with_textarea" id="a02"><field name="input">//========== 上电初始状态 ==========
//唤醒词 1 条 + 指令词 3 条。指令词的"回复语"必须留空 ——
//因为要等 STM32 把数值回传过来之后再播, 不能识别成功就自动播固定话。</field><next><block type="asr_speak" id="a03"><field name="VOICE_SET">小蝶-清新女声</field><field name="vol">10</field><field name="speed">10</field><next><block type="asr_welcom" id="a04"><field name="NAME">欢迎使用环境监测语音助手，请用天问五幺唤醒我。</field><next><block type="asr_sleep" id="a05"><field name="NAME">我退下了，请用天问五幺唤醒我</field><next><block type="asr_id" id="a06"><field name="ASR">天问五幺</field><field name="leibie">唤醒词</field><field name="huifu">我在呢</field><field name="id">0</field><next><block type="asr_id" id="a07"><field name="ASR">当前温度</field><field name="leibie">命令词</field><field name="huifu"></field><field name="id">1</field><next><block type="asr_id" id="a08"><field name="ASR">当前湿度</field><field name="leibie">命令词</field><field name="huifu"></field><field name="id">2</field><next><block type="asr_id" id="a09"><field name="ASR">当前光照</field><field name="leibie">命令词</field><field name="huifu"></field><field name="id">3</field></block></next></block></next></block></next></block></next></block></next></block></next></block></next></block></statement></block><block type="hardware_init" id="a20" x="0" y="430"><statement name="DO"><block type="block_with_textarea" id="a21"><field name="input">//========== 上电后初始化 ==========
//串口0(Serial): TX=PB_5  RX=PB_6, 115200 —— 接 STM32 的 PA9/PA10
//⚠️ STM32 的 printf 也在同一根线上! 播报时它会把调试信息一起收进来。
//   正式验证语音前, 请把 STM32 那侧 ESP8266_DEBUG 改回 0, 并尽量减少周期打印。</field><next><block type="vol_set" id="a22"><value name="num"><shadow type="asr_vol" id="a23"><field name="PIN">7</field></shadow></value><next><block type="serial_begin" id="a24"><field name="serial_select">Serial</field><field name="TX">PB_5</field><field name="RX">PB_6</field><value name="CONTENT"><shadow type="baud" id="a25"><field name="baud">115200</field></shadow></value></block></next></block></next></block></statement></block><block type="procedures_defnoreturn" id="a30" x="0" y="700"><field name="NAME">ASR_CODE</field><comment pinned="false" h="80" w="160">识别到指令词 → 向 STM32 要数据</comment><statement name="STACK"><block type="block_with_textarea" id="a31"><field name="input">//语音识别成功时被自动调用一次。
//三条指令词各自向 STM32 发一个请求字符串(自动换行)。</field><next><block type="set_state_enter_wakeup" id="a32"><value name="num"><shadow type="math_number" id="a33"><field name="NUM">15</field></shadow></value><next><block type="controls_switch_case" id="a34"><mutation elseif="3"></mutation><value name="IF0"><block type="asr_snid" id="a35"></block></value><value name="IF1"><block type="math_number" id="a36"><field name="NUM">1</field></block></value><statement name="DO1"><block type="serial_println" id="a37"><field name="serial_select">Serial</field><value name="CONTENT"><block type="text" id="a38"><field name="TEXT">TEMP</field></block></value></block></statement><value name="IF2"><block type="math_number" id="a39"><field name="NUM">2</field></block></value><statement name="DO2"><block type="serial_println" id="a40"><field name="serial_select">Serial</field><value name="CONTENT"><block type="text" id="a41"><field name="TEXT">HUMI</field></block></value></block></statement><value name="IF3"><block type="math_number" id="a42"><field name="NUM">3</field></block></value><statement name="DO3"><block type="serial_println" id="a43"><field name="serial_select">Serial</field><value name="CONTENT"><block type="text" id="a44"><field name="TEXT">LUX</field></block></value></block></statement></block></next></block></next></block></statement></block><block type="thread_app" id="a50" x="0" y="1050"><field name="app_name">UART_RX</field><field name="uxPriority">4</field><field name="mem">128</field><statement name="DO"><block type="block_with_textarea" id="a51"><field name="input">//========== 收 STM32 回传的数值并播报 ==========
//STM32 回 "25" → 这里以【数值模式】播报数字 25 → 喇叭念"二十五"</field><next><block type="controls_repeat_forever" id="a52"><statement name="DO"><block type="controls_if" id="a53"><value name="IF0"><block type="serial_available" id="a54"><field name="serial_select">Serial</field></block></value><statement name="DO0"><block type="asr_event_play_num" id="a55"><field name="mode">1</field><value name="num"><block type="serial_parseInt_Float" id="a56"><field name="serial_select">Serial</field><field name="STAT">parseInt</field></block></value></block></statement></block><next><block type="haodabit_DELAY1" id="a57"><value name="ioinput"><shadow type="math_number" id="a58"><field name="NUM">2</field></shadow></value></block></next></block></statement></block></next></block></statement></block><block type="thread_app" id="a60" x="0" y="1400"><field name="app_name">app</field><field name="uxPriority">4</field><field name="mem">128</field><statement name="DO"><block type="block_with_textarea" id="a61"><field name="input">//平台要求的空线程</field><next><block type="controls_repeat_forever" id="a62"><statement name="DO"><block type="haodabit_DELAY1" id="a63"><value name="ioinput"><shadow type="math_number" id="a64"><field name="NUM">100</field></shadow></value></block></statement></block></next></block></statement></block></xml>
<!-- edittype="asr_pro" -->
<!-- officialLib="[]" -->
<!-- personalLib="[]" -->
'@

# 先校验 XML 是否合法(不合法就不写)
try {
    $parsed = [xml]$xml
    $n = $parsed.xml.block.Count
    Write-Output ("XML 校验通过, 顶层积木块 = $n 个")
} catch {
    throw ("XML 不合法, 未写入: " + $_.Exception.Message)
}

# 统计所有 block type, 便于核对
$types = [regex]::Matches($xml, '<block type="([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
Write-Output "用到的积木类型:"
$types | Group-Object | Sort-Object Name | ForEach-Object { '   {0,-26} x{1}' -f $_.Name, $_.Count }

[System.IO.File]::WriteAllText($f, $xml, $utf8)
Write-Output ("WROTE {0}  ({1} 字符)" -f $f, $xml.Length)
