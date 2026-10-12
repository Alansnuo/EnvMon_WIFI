$ErrorActionPreference = 'Stop'

$f    = 'E:\tianwen\STM32智能家居语音系统.hd'
$bak  = 'E:\tianwen\STM32智能家居语音系统.hd.bak'
$utf8 = New-Object System.Text.UTF8Encoding($false)

if (-not (Test-Path $f)) { throw "找不到 $f" }

# 只在第一次运行前做备份(已有备份就不覆盖, 保住最原始那份)
if (-not (Test-Path $bak)) { Copy-Item $f $bak -Force; Write-Output "BACKUP -> $bak" }
else                       { Write-Output "BACKUP 已存在, 保留不动: $bak" }

$raw     = [System.IO.File]::ReadAllText($f, [System.Text.Encoding]::UTF8)
$useCRLF = $raw.Contains("`r`n")
$t       = $raw.Replace("`r`n", "`n")      # 匹配统一按 LF, 写回时再还原
Write-Output ("读入 {0} 字符, 换行 = {1}" -f $raw.Length, $(if($useCRLF){'CRLF'}else{'LF'}))

# ============================================================ 1
$o1 = @'
/* ------------------------------------------------------------
 * 向STM32发送一帧控制命令：55 AA 02 通道 数据 校验
 * 校验 = 0x100 - (前面所有字节之和的低8位)
 * ---------------------------------------------------------- */
static void tx_frame(uint8_t ch, uint8_t data)
{
    uint8_t sum;
    sum = (uint8_t)(0x55 + 0xAA + 0x02 + ch + data);
    Serial.write((uint8_t)0x55);
    Serial.write((uint8_t)0xAA);
    Serial.write((uint8_t)0x02);
    Serial.write(ch);
    Serial.write(data);
    Serial.write((uint8_t)(0x100 - sum));
}
'@
$n1 = @'
/* ------------------------------------------------------------
 * 向STM32发送一帧控制命令：55 AA 02 通道 数据 校验
 * 校验 = 前 5 个字节逐个异或 (XOR)
 *
 * ⚠️ 2026-10-10 由"和校验 0x100-sum"改成异或校验, 对齐《04 天问语音模块移植》
 *    和 STM32 侧 BSP/ASR/asr.c —— 原来两边算法不同, MCU 会把每一帧都判成
 *    校验失败, 结果是一个字节都收不到.
 *    例: 55 AA 02 01 00 -> 和校验 FE, 异或校验 FC
 * ---------------------------------------------------------- */
static void tx_frame(uint8_t ch, uint8_t data)
{
    uint8_t chk;
    chk = (uint8_t)(0x55 ^ 0xAA ^ 0x02 ^ ch ^ data);
    Serial.write((uint8_t)0x55);
    Serial.write((uint8_t)0xAA);
    Serial.write((uint8_t)0x02);
    Serial.write(ch);
    Serial.write(data);
    Serial.write(chk);
}
'@

# ============================================================ 2
$o2 = @'
/* ------------------------------------------------------------
 * 串口0接收状态机 + 查询命令超时兜底(独立任务)
 * STM32 -> 模块 帧格式：AA 55 LEN CMD DATA... SUM
 * LEN = CMD及DATA的字节总数，SUM = 0x100 - (前面所有字节和的低8位)
 * ---------------------------------------------------------- */
'@
$n2 = @'
/* ------------------------------------------------------------
 * 串口0接收状态机 + 查询命令超时兜底(独立任务)
 * STM32 -> 模块 帧格式：AA 55 LEN CMD DATA... XOR
 * LEN = CMD及DATA的字节总数，XOR = 前面所有字节逐个异或
 * ⚠️ 2026-10-10 校验已由"和"改成"异或", 见 tx_frame() 上面的说明
 * ---------------------------------------------------------- */
'@

# ============================================================ 3
$o3 = @'
                case 4:
                    st = 0;
                    sum = RX_HEAD1 + RX_HEAD2 + len + b;
                    for(idx = 0; idx < len; idx++) sum += buf[idx];
                    if(((uint8_t)(0x100 - (sum & 0xFF))) == b)
                    {
                        rx_deal(len, buf);   // 回帧在此完成播报
                        wait_cnt = 0;         // 已响应，清除计时
                    }
                    break;
'@
$n3 = @'
                case 4:
                    st = 0;
                    sum = (uint8_t)(RX_HEAD1 ^ RX_HEAD2 ^ len);
                    for(idx = 0; idx < len; idx++) sum = (uint8_t)(sum ^ buf[idx]);
                    if(sum == b)
                    {
                        rx_deal(len, buf);   // 回帧在此完成播报
                        wait_cnt = 0;         // 已响应，清除计时
                    }
                    break;
'@

# ============================================================ 4  定时播报任务
$o4 = @'
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------
 * 语音识别回调：只做分发，耗时动作交给独立任务
 * ---------------------------------------------------------- */
void ASR_CODE(){
'@
$n4 = @'
    vTaskDelete(NULL);
}

/* ============================================================
 *  定时播报任务 —— 必做3 里"定时"的那一半
 *
 *  为什么放在模块侧: 必做3 要求"天问语音模块**定时**或按按键触发播报".
 *    用户说"打开定时播报"(snid=9) -> g_timer_on=1 -> 本任务每 TIMER_PERIOD_S 秒
 *    向 STM32 发一帧 TX_CH_ENV_REQ(0x03); MCU 回 CMD_ENV(模式1) ->
 *    由 asr_app_task 里的 rx_deal() 完成播报.
 *  也就是说本任务只负责"到点喊一声", 播报内容和 1.5 秒超时兜底全部复用现成的.
 *
 *  ⚠️ 不要在这里直接播报: 播报要好几秒, 会把这个 1 秒节拍拖乱; 而且
 *     播放资源必须等上一条播完, 交给 rx_deal() 那条路径更稳.
 * ============================================================ */
#define TIMER_PERIOD_S   60             // 定时播报周期(秒), 想改快改这里

static volatile uint8_t g_timer_on = 0; // 定时播报开关, 由 snid 9/10 置位

void asr_timer_task()
{
    uint16_t sec    = TIMER_PERIOD_S;   // 初值=周期, 使"刚打开就立刻播一次"
    uint8_t  was_on = 0;

    while(1)
    {
        if(g_timer_on)
        {
            if(!was_on)                 // 刚被打开
            {
                was_on = 1;
                sec    = TIMER_PERIOD_S;
            }

            if(++sec >= TIMER_PERIOD_S)
            {
                sec = 0;
                /* 上一帧的回帧还没处理完, 就跳过这一轮, 避免和查询命令打架 */
                if(!g_req_pending)
                {
                    g_req_type    = 7;
                    g_req_arrived = 0;
                    g_req_pending = 1;
                    tx_frame(TX_CH_ENV_REQ, 0x01);
                }
            }
        }
        else
        {
            was_on = 0;
            sec    = 0;
        }

        delay(1000);
    }
}

/* ------------------------------------------------------------
 * 语音识别回调：只做分发，耗时动作交给独立任务
 * ---------------------------------------------------------- */
void ASR_CODE(){
'@

# ============================================================ 5  9/10 置开关
$o5 = @'
      case 9:  tx_frame(TX_CH_TIMER, 0x01);  break;   // 打开定时播报
      case 10: tx_frame(TX_CH_TIMER, 0x00);  break;   // 关闭定时播报
'@
$n5 = @'
      case 9:  g_timer_on = 1; tx_frame(TX_CH_TIMER, 0x01);  break;   // 打开定时播报
      case 10: g_timer_on = 0; tx_frame(TX_CH_TIMER, 0x00);  break;   // 关闭定时播报
'@

# ============================================================ 6  音量三档补动作
$o6 = @'
      case 13:                                       // 主动休眠
        enter_wakeup(300);
        delay(200);
        exit_wakeup_deal(0);
        break;
      default: break;
'@
$n6 = @'
      case 13:                                       // 主动休眠
        enter_wakeup(300);
        delay(200);
        exit_wakeup_deal(0);
        break;

      /* 音量三档: 词表里已经配了 ID 10250/10251/10252, 但原来 switch 里没有
       * 对应分支, 说了没反应. 播报音量范围 1~7 (见 hardware_init 的 vol_set(7)) */
      case 10250: vol_set(7); break;                 // 最大音量
      case 10251: vol_set(4); break;                 // 中等音量
      case 10252: vol_set(1); break;                 // 最小音量

      default: break;
'@

# ============================================================ 7  建定时任务
$o7 = @'
    xTaskCreate(asr_app_task,"asr_app",256,NULL,4,NULL);
'@
$n7 = @'
    xTaskCreate(asr_app_task,"asr_app",256,NULL,4,NULL);
    xTaskCreate(asr_timer_task,"asr_timer",256,NULL,3,NULL);
'@

$pairs = @(
    @($o1,$n1), @($o2,$n2), @($o3,$n3), @($o4,$n4),
    @($o5,$n5), @($o6,$n6), @($o7,$n7)
)

$i = 0
foreach ($p in $pairs)
{
    $i++
    $old = $p[0]
    $new = $p[1]
    $cnt = ([regex]::Matches($t, [regex]::Escape($old))).Count
    if ($cnt -ne 1) { throw ("PATCH {0} 匹配到 {1} 处 (应为 1) —— 什么都没写, 原文件未动" -f $i, $cnt) }
    $t = $t.Replace($old, $new)
    Write-Output ("PATCH {0} OK" -f $i)
}

$out = if ($useCRLF) { $t.Replace("`n", "`r`n") } else { $t }
[System.IO.File]::WriteAllText($f, $out, $utf8)
Write-Output ("WROTE {0}  ({1} 字符, 换行已还原为 {2})" -f $f, $out.Length, $(if($useCRLF){'CRLF'}else{'LF'}))

# ---------------- 复核 ----------------
Write-Output ''
Write-Output '===== 复核: 还有没有残留的和校验 ====='
$left = Select-String -Path $f -Pattern '0x100 - ' -ErrorAction SilentlyContinue
if ($left) { $left | ForEach-Object { '  RESIDUAL: ' + $_.Line.Trim() } } else { '  无残留 OK' }

Write-Output ''
Write-Output '===== 复核: 新加的东西在不在 ====='
foreach ($k in @('chk = (uint8_t)(0x55 ^ 0xAA', 'asr_timer_task', 'g_timer_on = 1', 'case 10250', 'sum = (uint8_t)(RX_HEAD1 ^ RX_HEAD2')) {
    $hit = Select-String -Path $f -Pattern ([regex]::Escape($k)) -ErrorAction SilentlyContinue
    if ($hit) { Write-Output ("  OK   {0}  (行 {1})" -f $k, (($hit | ForEach-Object { $_.LineNumber }) -join ',')) }
    else      { Write-Output ("  MISS {0}" -f $k) }
}
