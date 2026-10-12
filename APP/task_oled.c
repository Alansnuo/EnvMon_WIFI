/* ============================================================================
 * OLED 显示任务: 每 1 秒刷一次屏, 显示温湿度 / 光照
 * 只显示, 不采集 —— 数据从全局共享变量取 (见 app_main.h),
 * 避免和 DHT11 / Light 任务争抢总线时序
 * ========================================================================== */

#include "task_oled.h"
#include "app_main.h"
#include <stdio.h>

#include "OLED.h"

static void TaskOled(void *argument);

/* ========================= 建任务 ========================= */
void TaskOledCreate(void)
{
    if (xTaskCreate(TaskOled, "OLED", APP_STACK_OLED, NULL,
                    APP_PRIO_OLED, NULL) != pdPASS)
    {
        printf("ERROR: xTaskCreate(OLED) failed (heap too small?)\r\n");
        Error_Handler();
    }
}

/* ========================= 任务本体 ========================= */
static void TaskOled(void *argument)
{
    uint32_t    count = 0;
    DHT11_Data  dht;
    float       lux;
    unsigned int lux10;                 /* lux x10 取整, 用来拼出一位小数 */
    uint8_t      t, h;

    (void)argument;                     /* 未使用, 显式忽略 */

    /* 开机欢迎画面, 等 1 秒让采集任务先出数 */
    OLED_ShowString(1, 1, "Env Monitor");
    OLED_ShowString(2, 1, "Starting...");
    printf("[OLED] started, period %d ms\r\n", APP_OLED_PERIOD_MS);

    vTaskDelay(pdMS_TO_TICKS(1000));

    for (;;)
    {
        count++;

        /* 拷一份共享值. 两个 uint8_t 字段 + 一个 float, 用临界区保证一致性:
         * 采集任务可能在拷到一半时切过来改值. 进出临界区极短, 不影响采集. */
        taskENTER_CRITICAL();
        dht = g_xLatestDht11;          /* volatile -> 非 volatile, 直接赋值即可 */
        lux = g_xLatestLux;
        taskEXIT_CRITICAL();

        t = dht.temperature;
        h = dht.humidity;
        lux10 = (unsigned int)((lux * 10.0f) + 0.5f);

        /* Line 1: 标题 */
        OLED_ShowString(1, 1, "Env Monitor  ");

        /* Line 2: 温湿度. 整数部分 2 位够日常环境, 超过 99 也只显示低 2 位 */
        OLED_ShowString(2, 1, "T:   H:   ");
        OLED_ShowNum(2, 3, t, 2);
        OLED_ShowNum(2, 9, h, 2);

        /* Line 3: 光照 "L:xxxx.x lx" —— 用一位小数撑住分辨率 (BH1750 0.833 lx) */
        OLED_ShowString(3, 1, "L:        lx");
        OLED_ShowNum(3, 3, lux10 / 10U, 4);
        OLED_ShowChar (3, 7, '.');
        OLED_ShowNum(3, 8, lux10 % 10U, 1);

        /* Line 4: 刷新计数, 用来确认任务还活着 */
        OLED_ShowString(4, 1, "N:            ");
        OLED_ShowNum(4, 3, count, 5);

        /* OLED 刷屏不重, 但每次写数据要逐字节走 I2C, 全场约 80 字节.
         * 周期 1 秒绰绰有余, 不会拖累别的任务. */
        vTaskDelay(pdMS_TO_TICKS(APP_OLED_PERIOD_MS));
    }
}
