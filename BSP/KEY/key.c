/* ============================================================================
 * 4 个独立按键 —— 扫描式读取, 不用中断
 *
 * 写法参照 D:\Download\kile5\Project\C51\电子密码锁 的 BSP/Key/MatrixKey.c:
 * 参考工程就三行逻辑 ——
 *      if (P1_7 == 0) { Delay(20); while (P1_7 == 0); Delay(20); KeyNum = 1; }
 * 也就是"按下 -> 消抖 -> 等松手 -> 给编号"。本文件保持同样的直白,
 * 只是把"等松手"换成"记住没松手", 因为 FreeRTOS 任务里不能阻塞。
 *
 * 三个 static 数组, 每个键一格, 下标就是按键编号:
 *     s_Last   上一次采样的电平, 用来消抖 (1=松开, 0=按下)
 *     s_Sent   这次按住是不是已经报过了 (防止按住不放被反复当成新按下)
 *     s_Tick   按下那一刻的 HAL_GetTick(), 用来算按了多久
 *
 * ⚠️ 为什么不把 s_Last 和"确认按下"合成一个变量:
 *    消抖那一轮电平刚变, 还不能下结论; 要是把计时/报过标志也跟着清掉,
 *    松手那一轮会把状态搅乱, 结果就是"按了没反应"。这两个状态必须分开记。
 * ========================================================================== */

#include "key.h"

static uint8_t  s_Last[KEY_ID_NUM] = { 1U, 1U, 1U, 1U };
static uint8_t  s_Sent[KEY_ID_NUM] = { 0U, 0U, 0U, 0U };
static uint32_t s_Tick[KEY_ID_NUM] = { 0U, 0U, 0U, 0U };

/* ------------------------------ 初始化 ------------------------------ */
void Key_Init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();           /* KEY_PORT 是 GPIOA */

    gi.Pin   = KEY_ALL_PINS;
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_PULLUP;                 /* 松手=高, 按下=低 (所以不用外接电阻) */
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(KEY_PORT, &gi);

    /* 状态变量已经在定义时给了初值, 不用再 memset */
}

/* ------------------------------ 读电平 ------------------------------ */
/* 返回 1 = 松开(高), 0 = 按下(低)。直接按编号选引脚, 不用数组间接 */
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

    /* --- 消抖: 电平刚变, 这一轮先不下结论 ---
     * 下一轮还是同一个电平才算数; 变回去了就是抖动, 当没发生。 */
    if (now != s_Last[id])
    {
        s_Last[id] = now;
        return KEY_EVENT_NONE;
    }

    /* --- 稳定在"松开": 清掉"报过了", 下次按下才能再报 --- */
    if (now != 0U)
    {
        if (s_Sent[id] != 0U)
        {
            s_Sent[id] = 0U;
            return KEY_EVENT_RELEASE;
        }
        return KEY_EVENT_NONE;
    }

    /* --- 稳定在"按下" --- */
    if (s_Sent[id] == 0U)                   /* 这一轮刚确认按下 */
    {
        s_Sent[id] = 1U;
        s_Tick[id] = HAL_GetTick();
        return KEY_EVENT_PRESS;
    }

    /* 已经报过按下: 按住够久就报一次长按, 之后一直按住也不再报 */
    if ((uint32_t)(HAL_GetTick() - s_Tick[id]) >= KEY_LONG_MS)
    {
        if (s_Tick[id] != 0U)               /* 0 是"长按已经报过"的标记 */
        {
            s_Tick[id] = 0U;
            return KEY_EVENT_LONG;
        }
    }

    return KEY_EVENT_NONE;
}

/* ------------------------------ 取按键号 ------------------------------ */
/* 参考工程 MatrixKey() 那种用法: 按下给 1~4, 没动作给 0 */
uint8_t Key_GetNum(uint8_t id)
{
    if (Key_Scan(id) == KEY_EVENT_PRESS)
    {
        return (uint8_t)(id + 1U);
    }
    return 0U;
}
