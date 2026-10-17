/**
 * esp8266.c
 * ESP8266 WiFi 模块驱动实现
 *
 * 基于 USART2 (DMA 环形接收 + 中断发送), 把 WiFi / MQTT 操作翻译成 AT 指令.
 * 顺序阻塞: 每个接口内部"发一条命令 → 等应答 → 再发下一条", 等的时候调
 * BSP_DelayMs 让出 CPU, 返回前调用者不会往下走.
 *
 * 分区: 对外接口(初始化 / WiFi / 对时 / MQTT) → 内部实现(接收 / 命令收发)
 */

/* --------------------------------- 头文件 --------------------------------- */
#include "esp8266.h"
#include "bsp_delay.h"
#include <string.h>
#include <stdio.h>          /* snprintf: 拼 AT 指令用, 不走串口 */

/* -------------------------------- 私有变量 -------------------------------- */

/* 当前命令 (置于 RAM, 中断发送需访问) */
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
static uint16_t       ESP8266_RxPop(uint8_t dst[], uint16_t max);
static void           ESP8266_RxDrain(void);
static void           ESP8266_ClearRxBuf(void);

/* 命令收发 */
static ESP8266_Status ESP8266_Exec(const char ack[], uint32_t timeoutMs);
static ESP8266_Status ESP8266_SendCmd(const char cmd[], const char ack[], uint32_t timeoutMs);
static ESP8266_Status ESP8266_SendRaw(const char data[]);

/* ================================ 对外接口 ================================ */

/* --------------------------------- 初始化 --------------------------------- */

/**
 * @brief  初始化模块: 复位 + 挂上 DMA 接收 + 探活
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_Init(void)
{
    ESP8266_Status st;

    /* 先挂上 DMA 接收再复位, 从这一刻起串口来的字节就不会丢 */
    if (ESP8266_RxStart() != ESP8266_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 复位脉冲: 拉低 100ms 后释放 (模块启动日志为 74880 波特, 收进来是乱码) */
    HAL_GPIO_WritePin(ESP8266_RST_PORT, ESP8266_RST_PIN, GPIO_PIN_RESET);
    BSP_DelayMs(100);
    HAL_GPIO_WritePin(ESP8266_RST_PORT, ESP8266_RST_PIN, GPIO_PIN_SET);
    BSP_DelayMs(1000);

    /* 探活: 返回 OK 即模块正常 */
    st = ESP8266_SendCmd("AT\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 关闭回显, 后续应答匹配才不受干扰 */
    st = ESP8266_SendCmd("ATE0\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 先设成 Station(1). 必须排在 AT+CWAUTOCONN 之前: CWAUTOCONN 仅在
     * Station(1) / Station+AP(3) 下有效, 若模块 flash 中留有出厂 CWMODE=2(SoftAP),
     * 该命令直接返回 ERROR, 后面的流程全部无法进行.
     * CWMODE 掉电保存, 每次重发以免依赖 flash 中的既有值 */
    st = ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 关闭模块自身的自动重连 (会在后台占用射频状态, 导致 CWJAP 连不上).
     * 部分老固件/模式下该命令返回 ERROR, 属尽力而为, 失败忽略不影响初始化 */
    (void)ESP8266_SendCmd("AT+CWAUTOCONN=0\r\n", "OK", ESP8266_TIMEOUT);

    return ESP8266_OK;
}

/* ---------------------------------- WiFi ---------------------------------- */

/**
 * @brief  接热点 (含设成 Station 模式和清掉可能残留的关联)
 * @param  ssid      热点名
 * @param  password  密码
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_ConnectAP(const char ssid[], const char password[])
{
    ESP8266_Status st;

    /* CWMODE 掉电保存, 每次重发以免依赖模块 flash 中的既有值 */
    st = ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 清除可能残留的关联; 未连接时为空操作, 失败可继续 */
    (void)ESP8266_SendCmd("AT+CWQAP\r\n", "OK", ESP8266_TIMEOUT);

    /* AT+CWJAP="<名字>","<密码>" */
    (void)snprintf(s_Cmd, sizeof(s_Cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);

    /* 这一步包含关联 + DHCP, 须留足时间 */
    return ESP8266_Exec("OK", ESP8266_AP_TIMEOUT);
}

/* ---------------------------------- MQTT ---------------------------------- */

/* 模块自己讲协议(报文在它内部拼), 我们只管发 AT 指令 */

/**
 * @brief  配 MQTT 鉴权 + 连接参数 (底下三条指令: USERCFG / PASSWORD / CONNCFG).
 *         Token 124 字节超过 USERCFG 的 64 字节上限, 所以单独走 AT+MQTTPASSWORD;
 *         keepalive 必须显式给, 模块会把 0 强制改成 120
 * @param  clientId   设备名
 * @param  username   产品 ID
 * @param  password   鉴权 Token
 * @param  keepalive  心跳周期(s)
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_MQTT_SetParam(const char clientId[], const char username[],
                                     const char password[], uint16_t keepalive)
{
    ESP8266_Status st;

    /* 设备名和产品 ID; scheme=1 = MQTT over TCP (明文 1883) */
    (void)snprintf(s_Cmd, sizeof(s_Cmd),
        "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"\",0,0,\"\"\r\n", clientId, username);

    st = ESP8266_Exec("OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 密码单独发: Token 超过 MQTTUSERCFG 里 password 字段的 64 字节上限 */
    (void)snprintf(s_Cmd, sizeof(s_Cmd), "AT+MQTTPASSWORD=0,\"%s\"\r\n", password);

    st = ESP8266_Exec("OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* keepalive 必须显式给(模块把 0 改成 120 秒); 开清洁会话; 遗嘱不用 */
    (void)snprintf(s_Cmd, sizeof(s_Cmd),
        "AT+MQTTCONNCFG=0,%u,0,\"\",\"\",0,0\r\n", (unsigned)keepalive);

    return ESP8266_Exec("OK", ESP8266_TIMEOUT);
}

/**
 * @brief  连 broker. 必须匹配 "+MQTTCONNECTED", 不能只写 "CONNECTED"
 *         ("+MQTTDISCONNECTED" 里含着它)
 * @param  host  域名或 IP (传域名的话模块自己做 DNS)
 * @param  port  端口
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_MQTT_ConnectBroker(const char host[], uint16_t port)
{
    /* AT+MQTTCONN=0,"<host>",<port>,0 —— 域名中不含 " 和 ,, 可直接拼接;
     * <reconnect> 传 0: 模块不自行重连, 由任务层按"连续失败 N 次即整体重连"处理 */
    (void)snprintf(s_Cmd, sizeof(s_Cmd), "AT+MQTTCONN=0,\"%s\",%u,0\r\n",
                   host, (unsigned)port);

    /* 匹配 "+MQTTCONNECTED": "CONNECTED" 是 "+MQTTDISCONNECTED" 的子串 */
    return ESP8266_Exec("+MQTTCONNECTED", ESP8266_MQTT_TIMEOUT);
}


/**
 * @brief  发布一条消息 (QoS 0). 载荷里的 " 和 , 要调用方先转义成 \" 和 \,
 *         (见 task_wifi.c); 返回 OK 只代表模块收下了指令
 * @param  topic  主题
 * @param  data   载荷, 已转义
 * @retval ESP8266_OK = 成功, 其余 = 失败原因
 */
ESP8266_Status ESP8266_MQTT_Publish(const char topic[], const char data[])
{
    ESP8266_Status st;

    /* AT+MQTTPUB=0,"<主题>","<载荷>",0,0 —— 载荷中的 " 和 , 须已转义为
     * \" 和 \, (见 task_wifi.c): 模块按逗号切参数, 不转义会将 JSON 切开 */
    (void)snprintf(s_Cmd, sizeof(s_Cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n",
                   topic, data);

    st = ESP8266_Exec("OK", ESP8266_TIMEOUT);

    return st;
}

/* ================================== 对时 ================================== */

/* ESP-AT 原生 SNTP: 开启服务并固定时区, 之后直接读模块返回的格式化时间串.
 * SNTP 后台异步同步, 模块复位后首次联网需等其与 NTP 同步完成; 单轮给足时间,
 * 一轮未同步成功则重开 SNTP 再等一轮, 否则一次超时后会永久停在 00:00 */
#define NETTIME_MIN_YEAR         2020U            /* 年份早于此视为"还没同步上" */
#define NETTIME_SYNC_TIMEOUT     20000U           /* 单轮等 SNTP 同步的上限(ms) */
#define NETTIME_RETRY            2U               /* 同步轮数(含首轮) */

/**
 * @brief  用 ESP-AT 原生 SNTP 对一次时 (内部定死时区 UTC+8,
 *         直接解析模块回的格式化时间串), 必须在 ConnectAP() 成功之后调用
 * @param  hour  北京时间 时 (0~23)
 * @param  min   分 (0~59)
 * @param  sec   秒 (0~59)
 * @retval ESP8266_OK = 拿到时间, 其余 = 失败原因
 */
ESP8266_Status ESP8266_GetNetTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    ESP8266_Status st;
    char          *p;
    char          *q;
    uint32_t       tick;
    uint8_t        retry = 0U;

    if ((hour == NULL) || (min == NULL) || (sec == NULL))
    {
        return ESP8266_ERR_PARAM;
    }

    /* 打开 SNTP 并定死时区 UTC+8: 之后模块回显的就是北京时间 */
    st = ESP8266_SendCmd("AT+CIPSNTPCFG=1,8\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* SNTP 后台异步同步, 刚开时模块内部还是 1970 默认值, 所以要轮询:
     * 直到年份 >= 2020 才算真拿到时间, 否则超时收场 */
    tick = HAL_GetTick();
    for (;;)
    {
        /* 以 "OK" 收尾(而非 "CIPSNTPTIME:"), 确保整行时间串收全后再解析 */
        st = ESP8266_SendCmd("AT+CIPSNTPTIME?\r\n", "OK", ESP8266_TIMEOUT);
        if (st == ESP8266_OK)
        {

            /* "+CIPSNTPTIME:Fri Nov 17 17:09:22 2023":
             * 键名自带冒号, 其后的下一个冒号即 HH 与 MM 之间那个 */
            p = strstr((char *)s_RxBuf, "CIPSNTPTIME:");
            q = (p != NULL) ? strchr(p, ':') : NULL;
            q = (q != NULL) ? strchr(q + 1, ':') : NULL;

            if ((q != NULL) &&
                (q[-2] >= '0') && (q[-2] <= '9') && (q[-1] >= '0') && (q[-1] <= '9') &&
                (q[1]  >= '0') && (q[1]  <= '9') && (q[2]  >= '0') && (q[2]  <= '9') &&
                (q[3]  == ':') &&
                (q[4]  >= '0') && (q[4]  <= '9') && (q[5]  >= '0') && (q[5]  <= '9') &&
                (q[6]  == ' ') &&
                (q[7]  >= '0') && (q[7]  <= '9') && (q[8]  >= '0') && (q[8]  <= '9') &&
                (q[9]  >= '0') && (q[9]  <= '9') && (q[10] >= '0') && (q[10] <= '9') &&
                ((q[7] - '0') * 1000 + (q[8] - '0') * 100 + (q[9] - '0') * 10 + (q[10] - '0') >= NETTIME_MIN_YEAR))
            {
                *hour = (uint8_t)(((q[-2] - '0') * 10) + (q[-1] - '0'));
                *min  = (uint8_t)(((q[1]  - '0') * 10) + (q[2]  - '0'));
                *sec  = (uint8_t)(((q[4]  - '0') * 10) + (q[5]  - '0'));
                return ESP8266_OK;
            }
        }

        if ((HAL_GetTick() - tick) > NETTIME_SYNC_TIMEOUT)
        {
            /* 本轮未同步: 重开 SNTP 再给一轮时间 */
            if ((++retry) < NETTIME_RETRY)
            {
                (void)ESP8266_SendCmd("AT+CIPSNTPCFG=1,8\r\n", "OK", ESP8266_TIMEOUT);
                tick = HAL_GetTick();
                continue;
            }
            return ESP8266_ERR_TIMEOUT;
        }
        BSP_DelayMs(500);
    }
}

/* ================================ 内部实现 ================================ */

/* --------------------------- 接收: DMA 环形缓冲 --------------------------- */

/* DMA 在 CIRCULAR 模式下自动搬运字节, 这里只轮询 CNDTR 把新字节搬进解析缓冲.
 * 必须用 DMA: F1 的串口只有 1 字节接收寄存器, 而 DHT11 需关中断约 4ms,
 * 若用 RXNE 中断接收, 这 4ms 内的字节必然丢失 */

/* 启动 DMA 接收, 在 Init 中调用 (每次 Init 都要重新挂载) */
static ESP8266_Status ESP8266_RxStart(void)
{
    /* 必须把 UART 状态强制清回 READY, 否则重连会彻底失效:
     * HAL 的 Transmit_IT / Receive_DMA 都要求 gState / RxState == READY,
     * 否则直接返回 HAL_BUSY. 若上次 TX 超时未发完或 DMA 未收干净,
     * 状态会卡在 BUSY_TX / BUSY_RX, 之后每条命令都失败 —— 现象是模块已恢复
     * 却始终连不上, 只能在外层任务中不断重试.
     * HAL_UART_Abort() 一次复位收发两侧 (含 DMA 清理). */
    (void)HAL_UART_Abort(&ESP8266_UART);

    /* 三处都要清: 仅重置 s_DmaTail 不够, s_RxBuf / s_RxLen 是上一轮 Exec 的残留
     * (如被模块回显的命令), 不清会被计入"复位后收到的字节数" */
    memset(s_RxBuf, 0, sizeof(s_RxBuf));
    s_RxLen   = 0;
    s_DmaTail = 0;

    if (HAL_UART_Receive_DMA(&ESP8266_UART, s_DmaBuf,
                             ESP8266_DMA_BUF_SIZE) != HAL_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 关闭 DMA 传输完成中断: 环形模式下 HAL 的回调无操作, 改由轮询 CNDTR 获取进度 */
    __HAL_DMA_DISABLE_IT(ESP8266_UART.hdmarx, DMA_IT_TC);

    /* 串口错误中断也须关闭 (HAL 在 Receive_DMA 中会顺带打开 CR3.EIE): 否则帧错 /
     * 噪声 / 溢出都会被 HAL_UART_IRQHandler 视为致命错误并中止 DMA 接收 */
    __HAL_UART_DISABLE_IT(&ESP8266_UART, UART_IT_ERR);

    return ESP8266_OK;
}

/* DMA 当前的写指针: 环形模式下 "缓冲区大小 - CNDTR" 恒等于写位置 */
static uint16_t ESP8266_RxHead(void)
{
    return (uint16_t)(ESP8266_DMA_BUF_SIZE -
                      __HAL_DMA_GET_COUNTER(ESP8266_UART.hdmarx));
}

/* 从环形缓冲取至多 max 字节到 dst, 返回实际取出字节数 (0 = 无新数据) */
static uint16_t ESP8266_RxPop(uint8_t dst[], uint16_t max)
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

/* 把新字节搬入解析缓冲; 空间不足时少搬, 否则读指针将永远落后于 DMA */
static void ESP8266_RxDrain(void)
{
    uint16_t room = (uint16_t)(ESP8266_RX_BUF_SIZE - 1U - s_RxLen);

    s_RxLen = (uint16_t)(s_RxLen + ESP8266_RxPop(&s_RxBuf[s_RxLen], room));
}

/* 清接收缓冲: DMA 中未搬走的与已搬入解析缓冲的都要清, 只清后者会被回灌.
 * memset 同时保证缓冲区末尾恒为 '\0', strstr 才安全 */
static void ESP8266_ClearRxBuf(void)
{
    s_DmaTail = ESP8266_RxHead();
    memset(s_RxBuf, 0, sizeof(s_RxBuf));
    s_RxLen = 0;
}

/* -------------------------------- 命令收发 -------------------------------- */

/* 发送 s_Cmd 中拼好的命令并等应答, 超时或模块返回 ERROR/FAIL 时带原因返回 */
static ESP8266_Status ESP8266_Exec(const char ack[], uint32_t timeoutMs)
{
    uint16_t len = (uint16_t)strlen(s_Cmd);
    uint32_t tick;

    /* 超过 256 字节不发送: 截断的命令只会返回含义不明的 ERROR */
    if (len > ESP8266_AT_CMD_MAX)
    {
        return ESP8266_ERR_PARAM;
    }

    /* 发送前清接收缓冲, 避免上一条命令的应答混入 */
    ESP8266_ClearRxBuf();

    if (HAL_UART_Transmit_IT(&ESP8266_UART, (uint8_t *)s_Cmd, len) != HAL_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 判断是否发完只能看 gState: 接收 DMA 使 RxState 恒为 BUSY_RX */
    tick = HAL_GetTick();
    while (ESP8266_UART.gState != HAL_UART_STATE_READY)
    {
        if ((HAL_GetTick() - tick) > ESP8266_TX_TIMEOUT)
        {
            /* 超时须将 TX 状态清回 READY: 否则 gState 卡在 BUSY_TX,
             * 后续 Transmit_IT 持续返回 HAL_BUSY, 整条链路无法恢复 */
            (void)HAL_UART_AbortTransmit(&ESP8266_UART);
            return ESP8266_ERR_TIMEOUT;
        }
        BSP_DelayMs(1);
    }

    /* 判断顺序不可颠倒: 先匹配应答, 再看 ERROR/FAIL, 最后判超时 */
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

/* 发送字面量命令; 含用户数据的命令先拼进 s_Cmd 再直接调 Exec */
static ESP8266_Status ESP8266_SendCmd(const char cmd[], const char ack[], uint32_t timeoutMs)
{
    (void)strcpy(s_Cmd, cmd);

    return ESP8266_Exec(ack, timeoutMs);
}

/* 透传模式下发送原始数据 (HTTP 请求 / +++ 退出序列), 发完即返回, 不等应答 */
static ESP8266_Status ESP8266_SendRaw(const char data[])
{
    uint16_t len  = (uint16_t)strlen(data);
    uint32_t tick = HAL_GetTick();

    if (HAL_UART_Transmit_IT(&ESP8266_UART, (uint8_t *)data, len) != HAL_OK)
    {
        return ESP8266_ERR_AT;
    }

    while (ESP8266_UART.gState != HAL_UART_STATE_READY)
    {
        if ((HAL_GetTick() - tick) > ESP8266_TX_TIMEOUT)
        {
            (void)HAL_UART_AbortTransmit(&ESP8266_UART);
            return ESP8266_ERR_TIMEOUT;
        }
        BSP_DelayMs(1);
    }

    return ESP8266_OK;
}
