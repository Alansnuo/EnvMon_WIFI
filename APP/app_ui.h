/* ============================================================================
 * 界面状态: 时钟 + 阈值 + 菜单 + 当前页面
 *
 * 四者同属界面状态且相互耦合 (阈值由菜单修改, 页面决定绘制内容),
 * 合在一个模块, 避免 include 链互相依赖.
 *
 * 数据流单向: 按键任务只写, OLED 任务只读, 因此全部用 volatile 即可, 无需加锁.
 * (例外: 时钟秒脉冲由 OLED 任务自身产生, 见 AppClock_Tick)
 * ========================================================================== */

#ifndef __APP_UI_H
#define __APP_UI_H

#include <stdint.h>

/* ============================================================================
 * 一、时钟: WiFi 联网对时 + 软件走字 
 *
 * 时间来源: WiFi 任务联网成功后调 AppClock_SetTime() 写入 (见 task_wifi.c);
 * 秒进位:   OLED 任务周期性调用 AppClock_Tick() 推进.
 * 断电重启先显示 00:00:00, 联网对时后跳回真实时间.
 * ========================================================================== */

typedef struct {
    uint8_t hour;       /* 0~23 */
    uint8_t min;        /* 0~59 */
    uint8_t sec;        /* 0~59 */
} Clock_Time;

void       AppClock_Tick(void);     // 推进时钟
Clock_Time AppClock_Now(void);      // 获取当前时钟时间
void       AppClock_SetTime(uint8_t hour, uint8_t min, uint8_t sec); /* 联网对时 */

/* ============================================================================
 * 二、阈值: 运行时变量
 *
 *    TH_TEMP_UP 温度上限 ℃      TH_TEMP_DN 温度下限 ℃
 *    TH_HUMI_UP 湿度上限 %RH     TH_HUMI_DN 湿度下限 %RH
 *    TH_LUX_DN  光照下限 lx      TH_LUX_UP  光照上限 lx
 *
 * 仅存 RAM, 断电恢复默认值; 需掉电保存则另存 Flash.
 * ========================================================================== */

typedef enum {
    TH_TEMP_UP = 0,    // 温度上限 ℃
    TH_TEMP_DN,        // 温度下限 ℃
    TH_HUMI_UP,        // 湿度上限 %RH
    TH_HUMI_DN,        // 湿度下限 %RH
    TH_LUX_DN,         // 光照下限 lx
    TH_LUX_UP,         // 光照上限 lx
    TH_COUNT
} Thresh_Id;   // 阈值索引, 0~5

extern volatile int16_t g_xThresh[TH_COUNT];

int16_t Thresh_Get(Thresh_Id id);
void    Thresh_Adj(Thresh_Id id, int8_t dir);   /* dir = +1 / -1, 自动夹紧 */

/* ============================================================================
 * 三、菜单: 平铺列表, 6 项对应 6 个阈值 (温/湿/光 各上下限)
 *
 * 菜单页显示 ASCII 项名 (TempHi/...), 见 Menu_Name().
 * ========================================================================== */

#define MENU_COUNT      TH_COUNT                /* 一共 6 项 */

extern volatile uint8_t g_xMenuIdx;

const char *Menu_Name(uint8_t idx);
const char *Menu_Unit(uint8_t idx);
int16_t     Menu_Value(uint8_t idx);
void        Menu_Adj(uint8_t idx, int8_t dir);

/* ============================================================================
 * 四、页面: 按键任务切页, OLED 任务按页绘制
 *
 *   显示页  首页(WiFi) -> 温度 -> 湿度 -> 光照, KEY2/KEY3 循环切页
 *   菜单页  设置面: 列 6 项阈值, KEY2/KEY3 上下选, KEY4 进编辑
 *   编辑页  改选中项的值, KEY2/KEY3 加减
 * ========================================================================== */

#define UI_PAGE_ENV     0U      /* 首页: WiFi 连接状态 */
#define UI_PAGE_TEMP    1U      /* 温度显示 */
#define UI_PAGE_HUMI    2U      /* 湿度显示 */
#define UI_PAGE_LUX     3U      /* 光照显示 */
#define UI_PAGE_MENU    4U      /* 设置面第二级: 阈值列表 */
#define UI_PAGE_EDIT    5U      /* 编辑: 改选中项的值 */
#define UI_PAGE_SET     6U      /* 设置面第一级: 分类选择 */

extern volatile uint8_t g_xUiPage;

/* 设置面第一级: 温度 / 湿度 / 光照 / 阈值设置 */
#define SET_TEMP    0U
#define SET_HUMI    1U
#define SET_LUX     2U
#define SET_THRESH  3U
#define SET_COUNT   4U

extern volatile uint8_t g_xSetIdx;

const char *Set_Name(uint8_t idx);

/* 当前告警码 (0 = 正常, 其余见 asr.h 的 ASR_ALARM_xxx).
 * 仅由语音任务每秒计算并写入, OLED 只读 (单一计算点, 避免两处结果不一致);
 * OLED 据此做超限闪烁提示 */
extern volatile uint8_t g_xAlarmCode;

/* 用 app_config.h 的宏给阈值填初值, 在 App_Init() 里调 */
void UI_Init(void);

#endif /* __APP_UI_H */
