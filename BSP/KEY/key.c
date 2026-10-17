/* 4 个独立按键: 扫描式读取, 不用中断.
 * 三个 static 数组 (下标=键号): s_Last 上次电平(消抖), s_Sent 是否已报按下, s_Tick 按下时刻.
 * 消抖与"已报按下"分两个变量记录, 合并会使松手那一轮状态错乱.
 * 引脚(PA0/PA1/PA4/PA5, 输入+上拉)统一由 CubeMX 的 MX_GPIO_Init 配置, 本模块不再配引脚. */

#include "key.h"

static uint8_t  s_Last[KEY_ID_NUM] = { 1U, 1U, 1U, 1U }; // 按键引脚前一次电平
static uint8_t  s_Sent[KEY_ID_NUM] = { 0U, 0U, 0U, 0U }; // 按下事件是否已经发送上报，0=未报，1=已报
static uint32_t s_Tick[KEY_ID_NUM] = { 0U, 0U, 0U, 0U }; // 按下时刻(ms),值为0代表长按事件已上报
/* ------------------------------ 读电平 ------------------------------ */
/* 返回 1=松开(高), 0=按下(低) */
static uint8_t Key_Level(uint8_t id)
{
    switch (id)
    {
        case KEY_ID_1: return (HAL_GPIO_ReadPin(KEY_PORT, KEY1_PIN) == GPIO_PIN_SET) ? 1U : 0U;
        case KEY_ID_2: return (HAL_GPIO_ReadPin(KEY_PORT, KEY2_PIN) == GPIO_PIN_SET) ? 1U : 0U;
        case KEY_ID_3: return (HAL_GPIO_ReadPin(KEY_PORT, KEY3_PIN) == GPIO_PIN_SET) ? 1U : 0U;
        case KEY_ID_4: return (HAL_GPIO_ReadPin(KEY_PORT, KEY4_PIN) == GPIO_PIN_SET) ? 1U : 0U;
        default:       return 1U;
    }
}

/* ------------------------------ 扫描 ------------------------------ */
Key_Event Key_Scan(uint8_t id)
{
    uint8_t now;

    if (id >= KEY_ID_NUM)
    {
        return KEY_EVENT_NONE;
    }

    now = Key_Level(id);

    /* 消抖: 电平变化当轮不判定, 下一轮相同才确认 */
    if (now != s_Last[id])
    {
        s_Last[id] = now;
        return KEY_EVENT_NONE;
    }

    /* 稳定松开: 清已报标志, 下次按下可再报 */
    if (now != 0U)
    {
        if (s_Sent[id] != 0U)
        {
            s_Sent[id] = 0U;
            return KEY_EVENT_RELEASE;
        }
        return KEY_EVENT_NONE;
    }

    /* 稳定按下 */
    if (s_Sent[id] == 0U)                   /* 刚确认按下 */
    {
        s_Sent[id] = 1U;
        s_Tick[id] = HAL_GetTick();
        return KEY_EVENT_PRESS;
    }

    /* 已报按下: 按住够久报一次长按 */
    if ((uint32_t)(HAL_GetTick() - s_Tick[id]) >= KEY_LONG_MS)
    {
        if (s_Tick[id] != 0U)               /* 0 标记长按已报过 */
        {
            s_Tick[id] = 0U;
            return KEY_EVENT_LONG;
        }
    }

    return KEY_EVENT_NONE;
}

/* ------------------------------ 取按键号 ------------------------------ */
/* 按下给 1~4, 没动作给 0 */
uint8_t Key_GetNum(uint8_t id)
{
    if (Key_Scan(id) == KEY_EVENT_PRESS)
    {
        return (uint8_t)(id + 1U);
    }
    return 0U;
}
