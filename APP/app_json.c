/* ============================================================================
 * app_json.c
 * 上报报文: 感应器的值攒成一份 JSON, 由 WiFi 任务发给 OneNET 物模型
 * ========================================================================== */

#include "app_json.h"
#include <stdio.h>
#include <string.h>

/* 属性段是 \"Temp\":{\"value\":25.0}\,\"Hum\":{\"value\":60.0} 这一段,
 * 报文 = 报文头{\"id\",\"version\",\"params\"} + 属性段
 * (报文缓冲 JSON_TEXT_SIZE 在头文件里, 因为外面要读)
 *
 * 里面的 " 和 , 都多写了一层反斜杠: ESP8266 的 AT 指令按逗号切参数,
 * 载荷不转义会被当成分隔符切碎 —— 参考工程也是把转义符直接写在字面量里的 */
#define JSON_PARAMS_SIZE    64

/* ------------------------------ 感应器的值 ------------------------------ */

static int s_Temp;
static int s_Humi;

/* -------------------------------- 报文 --------------------------------- */

char            AppJson_Buf[JSON_TEXT_SIZE];    /* 拼好的报文 */
static char     s_Params[JSON_PARAMS_SIZE];     /* 当前值的属性段 */
static char     s_Sent[JSON_PARAMS_SIZE];       /* 上次发成功的属性段 */
static uint32_t s_MsgId;                        /* 报文序号 */

void AppJson_SetTemp(int temp)
{
    s_Temp = temp;
}

void AppJson_SetHumi(int humi)
{
    s_Humi = humi;
}

uint8_t AppJson_Report(void)
{
    /* 属性名必须和 OneNET 控制台"功能定义"里完全一致, 大小写敏感.
     * 值写 "%d.0": 属性是浮点型, 补 .0 又不用链进 float 版 printf */
    (void)snprintf(s_Params, sizeof(s_Params),
        "\\\"Temp\\\":{\\\"value\\\":%d.0}\\,"
        "\\\"Hum\\\":{\\\"value\\\":%d.0}",
        s_Temp, s_Humi);

    if (strcmp(s_Params, s_Sent) == 0)
    {
        return 0;                               /* 值没变, 不用发 */
    }

    (void)snprintf(AppJson_Buf, sizeof(AppJson_Buf),
        "{\\\"id\\\":\\\"%lu\\\"\\,\\\"version\\\":\\\"1.0\\\"\\,\\\"params\\\":{%s}}",
        (unsigned long)(++s_MsgId), s_Params);

    return 1;
}

void AppJson_Commit(void)
{
    (void)strcpy(s_Sent, s_Params);
}
