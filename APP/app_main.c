/* ============================================================================
 * 应用层总入口: 建数据通道 + 建任务
 * 只建不跑; 新增传感器在此加一句 TaskXxxCreate()
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
QueueHandle_t g_xEnvQueue = NULL;

/* ========================= 最新值共享 (采集任务写, OLED 任务读) ========================= */
volatile DHT11_Data g_xLatestDht11 = {0, 0};
volatile float      g_xLatestLux   = 0.0f;
volatile uint8_t    g_xWifiState   = WIFI_STATE_CONNECTING;

/* ========================= FreeRTOS 钩子 ========================= */

/* 栈溢出钩子 (configCHECK_FOR_STACK_OVERFLOW=2 要求); 死循环便于调试时定位 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    for (;;) { }
}

/* ========================= 应用层总入口 ========================= */
void App_Init(void)
{
    /* --- 1. 板级驱动初始化 --- */
    DHT11_Init();   
    BH1750_Init();
    Asr_Init();     
    OLED_Init();   
    PWM_Init();    
    UI_Init();     

    /* --- 2. 建采集 → 通信的数据通道 (温湿度 + 光照) --- */
    g_xEnvQueue = xQueueCreate(APP_ENV_QUEUE_LEN, sizeof(App_EnvMsg));
    if (g_xEnvQueue == NULL)
    {
        Error_Handler();
    }

    /* --- 3. 建任务 --- */
    TaskWifiCreate();
    TaskDht11Create();
    TaskLightCreate();
    TaskAsrCreate();
    TaskOledCreate();
    TaskKeyCreate();
}
