/* ============================================================================
 * 应用层总入口: 建数据通道 + 建任务
 * 只负责"建"不负责"跑" —— 以后加传感器, 在这里加一句 TaskXxxCreate() 即可
 * ========================================================================== */

#include "app_main.h"
#include "app_ui.h"

#include "BH1750.h"
#include "OLED.h"
#include "asr.h"
#include "dht11.h"
#include "key.h"
#include "pwm.h"
#include "task_asr.h"
#include "task_dht11.h"
#include "task_key.h"
#include "task_light.h"
#include "task_oled.h"
#include "task_wifi.h"

/* ========================= 任务间数据通道 ========================= */
QueueHandle_t g_xDht11Queue = NULL;

/* ========================= 最新值共享 (采集任务写, OLED 任务读) ========================= */
volatile DHT11_Data g_xLatestDht11 = {0, 0};
volatile float      g_xLatestLux   = 0.0f;
volatile uint8_t    g_xWifiState   = WIFI_STATE_CONNECTING;

/* ========================= FreeRTOS 钩子 ========================= */

/* 栈溢出钩子 (configCHECK_FOR_STACK_OVERFLOW=2 要求), 死循环便于调试定位 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    for (;;) { }
}

/* ========================= 应用层总入口 ========================= */
void App_Init(void)
{
    /* --- 1. 板级驱动初始化 (调度器之前: 内部延时不挡别人; ⚠️ 均不可在任务里重调) --- */
    DHT11_Init();   /* 内部有 1 秒上电延时 */
    BH1750_Init();
    Asr_Init();     /* 只开 USART1 RXNE 中断, UART 本体已由 CubeMX 配好 */
    OLED_Init();    /* 软件 I2C (PB8/PB9), 任务里重调会闪屏 */
    PWM_Init();     /* TIM1_CH1 → PA8, 先置灭, 之后由光照任务调 */
    Key_Init();     /* 只配引脚, 扫描在 task_key.c */
    UI_Init();      /* 阈值从 app_config.h 宏拷进运行时变量 */

    /* --- 2. 建 DHT11 → 通信的数据通道 --- */
    g_xDht11Queue = xQueueCreate(APP_DHT11_QUEUE_LEN, sizeof(DHT11_Data));
    if (g_xDht11Queue == NULL)
    {
        Error_Handler();
    }

    /* --- 3. 建任务 (只挂就绪表, 调度器起来后才跑; 运行顺序由优先级定) --- */
    TaskWifiCreate();
    TaskDht11Create();
    TaskLightCreate();
    TaskAsrCreate();
    TaskOledCreate();
    TaskKeyCreate();
}
