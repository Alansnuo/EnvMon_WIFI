/* ============================================================================
 * 应用层总入口: 建数据通道 + 建任务
 * 只负责"建"不负责"跑" —— 以后加传感器, 在这里加一句 TaskXxxCreate() 即可
 * ========================================================================== */

#include "app_main.h"
#include <stdio.h>

#include "BH1750.h"
#include "OLED.h"
#include "asr.h"
#include "dht11.h"
#include "task_asr.h"
#include "task_dht11.h"
#include "task_light.h"
#include "task_oled.h"
#include "task_wifi.h"

/* ========================= 任务间数据通道 ========================= */
QueueHandle_t g_xDht11Queue = NULL;

/* ========================= 最新值共享 (给 OLED 显示任务用) =========================
 * 采集任务写, OLED 任务读. 定义见 app_main.h 的说明. */
volatile DHT11_Data g_xLatestDht11 = {0, 0};
volatile float      g_xLatestLux   = 0.0f;

/* ========================= 应用层总入口 ========================= */
void App_Init(void)
{
    /* --- 1. 板级驱动初始化 ---
     * 放在调度器之前: 这里还没有别的任务, DHT11_Init() 内部那个 1 秒延时
     * 不会影响谁. 等调度器起来后, 驱动里同样这句 BSP_DelayMs 会自动变成
     * "挂起本任务", 驱动不用改 —— 机制见 BSP/bsp_delay.h. */
    DHT11_Init();
    printf("DHT11 Sensor Ready!\r\n");

    /* 光照同样放在调度器之前: 内部只有一次 10ms 的 BSP_DelayMs, 忙等得起.
     * ⚠️ 不要在任务里再调 BH1750_Init() —— 它会重配 PB6/PB7 并重发上电命令. */
    BH1750_Init();
    printf("BH1750 Sensor Ready!\r\n");

    /* 天问语音: 只打开 USART1 的 RXNE 中断. USART1 本身由 MX_USART1_UART_Init() 配好,
     * 而且它和 printf(fputc) 共用 —— 共用为什么安全见 BSP/ASR/asr.h. */
    Asr_Init();
    printf("ASR (TianWen) Ready!\r\n");

    /* OLED 软件 I2C (PB8=SCL, PB9=SDA). 内部有上电延时, 放调度器前才不挡别人.
     * ⚠️ 别在任务里再调 OLED_Init(): 它会重配 PB8/PB9 + 全屏清零 + 上电延时, 屏会闪. */
    OLED_Init();
    printf("OLED Ready!\r\n");

    /* --- 2. 建 DHT11 → 通信的数据通道 --- */
    g_xDht11Queue = xQueueCreate(APP_DHT11_QUEUE_LEN, sizeof(DHT11_Data));
    if (g_xDht11Queue == NULL)
    {
        printf("ERROR: xQueueCreate failed (heap too small?)\r\n");
        Error_Handler();
    }

    /* --- 3. 建任务 ---
     * 这几句只是把任务挂到就绪表上, 它们要等 main() 里那句
     * vTaskStartScheduler() 之后才真正开始跑. 谁先建不影响谁先跑,
     * 跑的顺序由优先级决定.
     * 下面几个函数内部会打印错误并进 Error_Handler, 不用查返回值. */
    TaskWifiCreate();
    TaskDht11Create();
    TaskLightCreate();
    TaskAsrCreate();
    TaskOledCreate();
}
