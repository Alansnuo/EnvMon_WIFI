/**
 * esp8266.c
 * ESP8266 WiFi 模块驱动函数实现
 * @date 2026.09.23
 *
 * 说明: 基于 USART2 (DMA 环形接收 + 中断发送), 把 WiFi / MQTT 操作翻译成 AT 指令.
 *       顺序阻塞写法: 每个接口内部就是"发一条命令 → 等应答 → 再发下一条", 等的时候
 *       通过 BSP_DelayMs 让出 CPU (任务里是 vTaskDelay, 裸机里是 HAL_Delay), 调用者
 *       不用管推进. 只适合"有独立任务专门跑网络"的用法(本项目的 WiFi 任务), 别放进
 *       裸机主循环: 一次连不上的流程最坏能连续占一分钟.
 *
 * 分区: 对外接口(初始化 / WiFi / MQTT) → 内部实现(接收 / 拼命令 / 命令收发)
 */

/* --------------------------------- 头文件 --------------------------------- */
#include "esp8266.h"
#include "bsp_delay.h"
#include <string.h>
#include <stdio.h>

/* -------------------------------- 外部变量 -------------------------------- */
extern UART_HandleTypeDef huart2;

/* --------------------------------- 宏定义 --------------------------------- */

/* 拼命令时给收尾字面量留的余量 ("\",0,0\r\n" 这类最长 15 字节) */
#define ESP8266_CMD_TAIL_ROOM   16U

/* -------------------------------- 私有变量 -------------------------------- */

/* 当前命令 (在 RAM 里, 中断发送要用) */
static char     s_Cmd[ESP8266_CMD_BUF_SIZE];

/* 接收 */
static uint8_t  s_DmaBuf[ESP8266_DMA_BUF_SIZE];         /* DMA 直接往这里写(环形) */
static uint16_t s_DmaTail;                              /* 我们搬到哪了(读指针) */
static uint8_t  s_RxBuf[ESP8266_RX_BUF_SIZE];           /* 解析缓冲: 累积整条应答 */
static uint16_t s_RxLen;

/* ------------------------------ 内部函数声明 ------------------------------ */

/* 接收 */
static ESP8266_Status ESP8266_RxStart(void);
static uint16_t       ESP8266_RxHead(void);
static uint16_t       ESP8266_RxPop(uint8_t *dst, uint16_t max);
static void           ESP8266_RxDrain(void);
static void           ESP8266_ClearRxBuf(void);

/* 拼命令 */
static uint8_t        ESP8266_AppendEsc(const char *src);

/* 命令收发 */
static ESP8266_Status ESP8266_Exec(const char *ack, uint32_t timeoutMs);
static ESP8266_Status ESP8266_SendCmd(const char *cmd, const char *ack, uint32_t timeoutMs);

/* ================================ 对外接口 ================================ */

/* --------------------------------- 初始化 --------------------------------- */

/**
 * @brief  初始化模块: 配 RST 引脚 + 挂上 DMA 接收 + 复位探活, 见 esp8266.h
 */
ESP8266_Status ESP8266_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    ESP8266_Status   st;

    /* RST 引脚由驱动自己配 (CubeMX 那边没配这个脚): PB1 推挽输出 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin   = ESP8266_RST_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ESP8266_RST_PORT, &GPIO_InitStruct);

    /* 先挂上 DMA 接收再复位: 从这一刻起不管 CPU 在忙什么(哪怕在关中断读
     * DHT11), 串口来的字节都不丢 */
    if (ESP8266_RxStart() != ESP8266_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 复位: 拉低 100ms 再放开. 模块的启动日志是 74880 波特, 收进来是一串乱码,
     * 不用管它 —— 真正算数的是下面 AT 回 OK */
    HAL_GPIO_WritePin(ESP8266_RST_PORT, ESP8266_RST_PIN, GPIO_PIN_RESET);
    BSP_DelayMs(100);
    HAL_GPIO_WritePin(ESP8266_RST_PORT, ESP8266_RST_PIN, GPIO_PIN_SET);
    BSP_DelayMs(1000);

    /* 探活: 回 OK 就是活着 */
    st = ESP8266_SendCmd("AT\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 关回显, 后面匹配应答才干净 */
    st = ESP8266_SendCmd("ATE0\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 关掉模块自己的自动重连: 它会在后台跟我们抢射频状态, 症状是"扫得到、
     * 信号满格, 但 CWJAP 就是连不上" */
    return ESP8266_SendCmd("AT+CWAUTOCONN=0\r\n", "OK", ESP8266_TIMEOUT);
}

/* ---------------------------------- WiFi ---------------------------------- */

/**
 * @brief  接热点, 见 esp8266.h
 */
ESP8266_Status ESP8266_ConnectAP(const char *ssid, const char *password)
{
    ESP8266_Status st;

    if (ssid == NULL || password == NULL)
    {
        return ESP8266_ERR_PARAM;
    }

    /* CWMODE 是掉电保存的, 每次重发一遍不费事, 也省得依赖模块 flash 里存了什么 */
    st = ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 先清掉可能残留的关联: 模块卡在"半连不连"时不先清干净会一直连不上, 表现
     * 得很像密码错. 没连的时候它是空操作, 所以失败也接着往下走 */
    (void)ESP8266_SendCmd("AT+CWQAP\r\n", "OK", ESP8266_TIMEOUT);

    /* AT+CWJAP="<名字>","<密码>": 名字和密码都是用户数据, 里面的 " \ , 都得转义 */
    (void)strcpy(s_Cmd, "AT+CWJAP=\"");
    if (ESP8266_AppendEsc(ssid) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)strcat(s_Cmd, "\",\"");
    if (ESP8266_AppendEsc(password) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)strcat(s_Cmd, "\"\r\n");

    /* 这一步包含关联 + DHCP, 要留够时间 */
    return ESP8266_Exec("OK", ESP8266_AP_TIMEOUT);
}

/* ---------------------------------- MQTT ---------------------------------- */

/* 模块自己讲协议(报文在它内部拼), 我们只管发 AT 指令, 主题和载荷原样交给它.
 * 指令名以固件实际支持的为准(扫二进制字符串表确认过): 老资料里的 AT+MQTTCFG
 * 在这儿叫 AT+MQTTUSERCFG + AT+MQTTCONNCFG, 心跳由模块自己按 keepalive 发. */

/**
 * @brief  配 MQTT 鉴权参数和连接参数, 见 esp8266.h
 */
ESP8266_Status ESP8266_MQTT_SetParam(const char *clientId, const char *username,
                                     const char *password, uint16_t keepalive)
{
    ESP8266_Status st;

    if (clientId == NULL || username == NULL || password == NULL)
    {
        return ESP8266_ERR_PARAM;
    }

    /* 设备名和产品 ID; scheme=1 = MQTT over TCP (明文 1883); 证书 ID 和 path
     * 只有 WebSocket 用 */
    (void)strcpy(s_Cmd, "AT+MQTTUSERCFG=0,1,\"");
    if (ESP8266_AppendEsc(clientId) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)strcat(s_Cmd, "\",\"");
    if (ESP8266_AppendEsc(username) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)strcat(s_Cmd, "\",\"\",0,0,\"\"\r\n");

    st = ESP8266_Exec("OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 密码单独发: Token 124 字节 > MQTTUSERCFG 里 password 字段的 64 字节上限
     * (文档要求这条放在 USERCFG 之后) */
    (void)strcpy(s_Cmd, "AT+MQTTPASSWORD=0,\"");
    if (ESP8266_AppendEsc(password) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)strcat(s_Cmd, "\"\r\n");

    st = ESP8266_Exec("OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 连接参数. keepalive 必须显式给: 模块把 0 强制改成 120 秒.
     * disable_clean_session=0 = 开清洁会话 (关掉会留下旧会话, 表现成"连上了但
     * 发布没反应"); 后面是遗嘱, 不用 */
    (void)sprintf(s_Cmd, "AT+MQTTCONNCFG=0,%u,0,\"\",\"\",0,0\r\n", (unsigned)keepalive);

    return ESP8266_Exec("OK", ESP8266_TIMEOUT);
}

/**
 * @brief  连 MQTT Broker, 见 esp8266.h
 */
ESP8266_Status ESP8266_MQTT_ConnectBroker(const char *host, uint16_t port)
{
    if (host == NULL)
    {
        return ESP8266_ERR_PARAM;
    }

    /* AT+MQTTCONN=0,"<host>",<port>,0
     * 最后的 <reconnect> 传 0: 不让模块自己闷头重连, 重连交给任务层那套
     * "连续失败 N 次就整个重来"(它会复位模块 + 重接热点, 更彻底) */
    (void)strcpy(s_Cmd, "AT+MQTTCONN=0,\"");
    if (ESP8266_AppendEsc(host) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)sprintf(&s_Cmd[strlen(s_Cmd)], "\",%u,0\r\n", (unsigned)port);

    /* 等 "+MQTTCONNECTED" 而不是 "CONNECTED": 后者是 "+MQTTDISCONNECTED" 的
     * 子串, 用短的会把"已经断开"读成"连上了" */
    return ESP8266_Exec("+MQTTCONNECTED", ESP8266_MQTT_TIMEOUT);
}

/**
 * @brief  发布一条消息, 见 esp8266.h
 */
ESP8266_Status ESP8266_MQTT_Publish(const char *topic, const char *data)
{
    if (topic == NULL || data == NULL)
    {
        return ESP8266_ERR_PARAM;
    }

    /* AT+MQTTPUB=0,"<主题>","<载荷>",0,0
     * 两段都要转义 —— JSON 载荷里全是逗号和引号, 不转义模块会当成多余的参数,
     * 回一句 +MQTTPUB:FAIL */
    (void)strcpy(s_Cmd, "AT+MQTTPUB=0,\"");
    if (ESP8266_AppendEsc(topic) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)strcat(s_Cmd, "\",\"");
    if (ESP8266_AppendEsc(data) == 0U)
    {
        return ESP8266_ERR_PARAM;
    }
    (void)strcat(s_Cmd, "\",0,0\r\n");

    return ESP8266_Exec("OK", ESP8266_TIMEOUT);
}

/* ================================ 内部实现 ================================ */

/* --------------------------- 接收: DMA 环形缓冲 --------------------------- */

/* DMA 在 CIRCULAR 模式下自己搬字节, CPU 在忙什么、中断开不开都不影响它, 我们
 * 只轮询 CNDTR 把新字节搬进解析缓冲.
 * 接收非用 DMA 不可的原因: F1 的串口只有 1 字节接收寄存器, 没有 FIFO, 而
 * DHT11 那边要关中断约 4ms, 用 RXNE 中断收的话那 4ms 里的字节必丢.
 *   s_DmaBuf  环形, 只要装下两次搬运之间的突发 (1ms @115200 才 11 字节)
 *   s_RxBuf   直线, 要累积到整条应答匹配完为止, 所以更大 */

/* 启动 DMA 接收. Init 里调 (每次 Init 都要重挂) */
static ESP8266_Status ESP8266_RxStart(void)
{
    /* 先停再开, 这一步不能省: Receive_DMA 要求 RxState == READY, 而重连时 DMA
     * 还在环形模式里跑(RxState=BUSY_RX), 不停掉就永远失败 —— 症状是"第一次
     * 失败之后永远失败", 看着像模块坏了. */
    (void)HAL_UART_DMAStop(&ESP8266_UART);

    s_DmaTail = 0;

    if (HAL_UART_Receive_DMA(&ESP8266_UART, s_DmaBuf,
                             ESP8266_DMA_BUF_SIZE) != HAL_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 关掉 DMA 传输完成中断: 环形模式下 HAL 的回调什么都不做, 我们靠轮询
     * CNDTR 知道进度. */
    __HAL_DMA_DISABLE_IT(ESP8266_UART.hdmarx, DMA_IT_TC);

    /* 串口自己的错误中断(HAL 在 Receive_DMA 里顺手开的 CR3.EIE)也要关:
     * 开了 USART2 中断之后, 帧错/噪声/溢出任一标志都会让 HAL_UART_IRQHandler
     * 当成致命错误、直接中止 DMA 接收, 接收就此全哑. 模块每次复位吐的启动日志
     * 是 74880 波特, 收进来就是一串帧错, 正好踩中. 我们靠轮询, 不需要它. */
    __HAL_UART_DISABLE_IT(&ESP8266_UART, UART_IT_ERR);

    return ESP8266_OK;
}

/* DMA 当前的写指针: 环形模式下 "缓冲区大小 - CNDTR" 恒等于写位置 */
static uint16_t ESP8266_RxHead(void)
{
    return (uint16_t)(ESP8266_DMA_BUF_SIZE -
                      __HAL_DMA_GET_COUNTER(ESP8266_UART.hdmarx));
}

/* 从环形缓冲取最多 max 字节到 dst, 返回实际取了多少(0 = 没新数据).
 * 唯一推进读指针的地方 */
static uint16_t ESP8266_RxPop(uint8_t *dst, uint16_t max)
{
    uint16_t head  = ESP8266_RxHead();
    uint16_t moved = 0;

    while (s_DmaTail != head && moved < max)
    {
        dst[moved++] = s_DmaBuf[s_DmaTail++];

        if (s_DmaTail >= ESP8266_DMA_BUF_SIZE)
        {
            s_DmaTail = 0;
        }
    }

    return moved;
}

/* 把新字节搬进解析缓冲. 放不下就少搬点 —— 读指针不能卡住, 卡住就追不上 DMA
 * 了; 满没满由 Exec() 判(ERR_BUF_FULL) */
static void ESP8266_RxDrain(void)
{
    uint16_t room = (uint16_t)(ESP8266_RX_BUF_SIZE - 1U - s_RxLen);

    s_RxLen = (uint16_t)(s_RxLen + ESP8266_RxPop(&s_RxBuf[s_RxLen], room));
}

/* 清接收缓冲: DMA 里没搬走的和已搬进解析缓冲的都要清 —— 只清后者, 下次
 * Drain 又会被前者灌回来.
 * memset 不只是清内容: 新追加的字节靠它保证末尾恒为 '\0', strstr 才安全 */
static void ESP8266_ClearRxBuf(void)
{
    s_DmaTail = ESP8266_RxHead();
    memset(s_RxBuf, 0, sizeof(s_RxBuf));
    s_RxLen = 0;
}

/* --------------------------------- 拼命令 --------------------------------- */

/* 把 src 转义后接到 s_Cmd 末尾 (前面的内容不动), 放不下返回 0.
 * 转义 = 给 " \ , 前面补一个 '\'; 为什么非转不可: AT 指令按逗号切参数, 载荷里
 * 那几个内层逗号不转义就会被切成多余的参数, 模块回 ERROR (发布时是 +MQTTPUB:FAIL).
 * 三种字符必须一趟处理完, 分两趟会把第一趟补的 '\' 再转一遍, 得到 \\\" . */
static uint8_t ESP8266_AppendEsc(const char *src)
{
    uint16_t n    = (uint16_t)strlen(s_Cmd);
    uint16_t room;

    /* 最多拼到 256 - 16: 超过 256 的命令 Exec() 本来也不会发出去; 留出这段
     * 余量是为了让后面 strcat / sprintf 接的收尾字面量一定放得下 */
    room = (n < (uint16_t)(ESP8266_AT_CMD_MAX - ESP8266_CMD_TAIL_ROOM))
           ? (uint16_t)(ESP8266_AT_CMD_MAX - ESP8266_CMD_TAIL_ROOM - n)
           : 0U;

    while (*src != '\0')
    {
        if (*src == '"' || *src == '\\' || *src == ',')
        {
            if (room < 3U) { break; }                   /* 补的 '\' + 字符 + '\0' */
            s_Cmd[n++] = '\\';
            room--;
        }
        else if (room < 2U)
        {
            break;
        }

        s_Cmd[n++] = *src++;
        room--;
    }

    s_Cmd[n] = '\0';

    return (*src == '\0') ? 1U : 0U;                    /* 没写完就是放不下 */
}

/* -------------------------------- 命令收发 -------------------------------- */

/* 发 s_Cmd 里拼好的命令并等应答, 超时或模块回 ERROR/FAIL 就带着原因返回.
 * 等的时候调 BSP_DelayMs(1) 让出 CPU —— 这也是本驱动必须跑在独立任务里的原因 */
static ESP8266_Status ESP8266_Exec(const char *ack, uint32_t timeoutMs)
{
    uint16_t len = (uint16_t)strlen(s_Cmd);
    uint32_t tick;

    /* 超过 256 字节就不发: 半条命令发给模块只会回来一句含义模糊的 ERROR, 超长
     * 时它还可能静默截断 —— 让上层报 ERR_PARAM 更清楚 */
    if (len > ESP8266_AT_CMD_MAX)
    {
        return ESP8266_ERR_PARAM;
    }

    /* 发之前清接收缓冲: 上一条命令的应答别混进来 */
    ESP8266_ClearRxBuf();

    if (HAL_UART_Transmit_IT(&ESP8266_UART, (uint8_t *)s_Cmd, len) != HAL_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 判"发完没有"只能看 gState: 接收 DMA 让 RxState 一直是 BUSY_RX,
     * HAL_UART_GetState() 永远等不到 READY */
    tick = HAL_GetTick();
    while (ESP8266_UART.gState != HAL_UART_STATE_READY)
    {
        if ((HAL_GetTick() - tick) > ESP8266_TX_TIMEOUT)
        {
            return ESP8266_ERR_TIMEOUT;
        }
        BSP_DelayMs(1);
    }

    /* 等应答. 顺序不能反: 先看期望的应答, 再看 ERROR/FAIL, 最后才判超时 ——
     * 最后一个字节恰好凑齐应答时, 先判超时会把成功报成失败.
     * s_RxBuf 末尾恒为 '\0', strstr 不会越界. */
    tick = HAL_GetTick();
    for (;;)
    {
        ESP8266_RxDrain();

        if (strstr((char *)s_RxBuf, ack) != NULL)
        {
            return ESP8266_OK;
        }

        if ((strstr((char *)s_RxBuf, "ERROR") != NULL) ||
            (strstr((char *)s_RxBuf, "FAIL") != NULL))
        {
            return ESP8266_ERR_AT;
        }

        if (s_RxLen >= ESP8266_RX_BUF_SIZE - 1U)
        {
            return ESP8266_ERR_BUF_FULL;
        }

        if ((HAL_GetTick() - tick) > timeoutMs)
        {
            return ESP8266_ERR_TIMEOUT;
        }

        BSP_DelayMs(1);
    }
}

/* 发一条字面量命令. 带用户数据的那些(要转义)先把命令拼进 s_Cmd, 再直接调 Exec */
static ESP8266_Status ESP8266_SendCmd(const char *cmd, const char *ack, uint32_t timeoutMs)
{
    (void)strcpy(s_Cmd, cmd);

    return ESP8266_Exec(ack, timeoutMs);
}
