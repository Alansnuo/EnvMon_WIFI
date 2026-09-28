/**
 * esp8266.c
 * ESP8266 WiFi 模块驱动函数实现
 * @date 2026.09.23
 *
 * 基于 USART2 (DMA 环形接收 + 中断发送), 把 WiFi / MQTT 操作翻译成 AT 指令.
 * 顺序阻塞: 每个接口内部"发一条命令 → 等应答 → 再发下一条", 等的时候调
 * BSP_DelayMs 让出 CPU, 返回前调用者不会往下走.
 *
 * 分区: 对外接口(初始化 / WiFi / MQTT) → 内部实现(接收 / 命令收发)
 */

/* --------------------------------- 头文件 --------------------------------- */
#include "esp8266.h"
#include "bsp_delay.h"
#include <string.h>
#include <stdio.h>

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
static uint16_t       ESP8266_RxPop(uint8_t dst[], uint16_t max);
static void           ESP8266_RxDrain(void);
static void           ESP8266_ClearRxBuf(void);

/* 命令收发 */
static ESP8266_Status ESP8266_Exec(const char ack[], uint32_t timeoutMs);
static ESP8266_Status ESP8266_SendCmd(const char cmd[], const char ack[], uint32_t timeoutMs);

/* ================================ 对外接口 ================================ */

/* --------------------------------- 初始化 --------------------------------- */

/**
 * @brief  初始化模块: 复位 + 挂上 DMA 接收 + 探活, 见 esp8266.h
 */
ESP8266_Status ESP8266_Init(void)
{
    ESP8266_Status st;

    /* 先挂上 DMA 接收再复位, 从这一刻起串口来的字节就不会丢 */
    if (ESP8266_RxStart() != ESP8266_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 复位脉冲: 拉低 100ms 再放开 (模块启动日志是 74880 波特, 收进来是乱码) */
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

    /* 关掉模块自己的自动重连: 它会在后台抢射频状态, 导致 CWJAP 连不上 */
    return ESP8266_SendCmd("AT+CWAUTOCONN=0\r\n", "OK", ESP8266_TIMEOUT);
}

/* ---------------------------------- WiFi ---------------------------------- */

/**
 * @brief  接热点, 见 esp8266.h
 */
ESP8266_Status ESP8266_ConnectAP(const char ssid[], const char password[])
{
    ESP8266_Status st;

    /* CWMODE 是掉电保存的, 每次重发一遍不费事, 也省得依赖模块 flash 里存了什么 */
    st = ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", ESP8266_TIMEOUT);
    if (st != ESP8266_OK)
    {
        return st;
    }

    /* 清掉可能残留的关联; 没连的时候是空操作, 所以失败也往下走 */
    (void)ESP8266_SendCmd("AT+CWQAP\r\n", "OK", ESP8266_TIMEOUT);

    /* AT+CWJAP="<名字>","<密码>" */
    (void)snprintf(s_Cmd, sizeof(s_Cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);

    /* 这一步包含关联 + DHCP, 要留够时间 */
    return ESP8266_Exec("OK", ESP8266_AP_TIMEOUT);
}

/* ---------------------------------- MQTT ---------------------------------- */

/* 模块自己讲协议(报文在它内部拼), 我们只管发 AT 指令 */

/**
 * @brief  配 MQTT 鉴权参数和连接参数, 见 esp8266.h
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
 * @brief  连 MQTT Broker, 见 esp8266.h
 */
ESP8266_Status ESP8266_MQTT_ConnectBroker(const char host[], uint16_t port)
{
    /* AT+MQTTCONN=0,"<host>",<port>,0 —— 域名里不会有 " 和 ,, 直接拼;
     * <reconnect> 传 0: 模块不自己重连, 交给任务层"连续失败 N 次就整个重来" */
    (void)snprintf(s_Cmd, sizeof(s_Cmd), "AT+MQTTCONN=0,\"%s\",%u,0\r\n",
                   host, (unsigned)port);

    /* 匹配 "+MQTTCONNECTED": "CONNECTED" 是 "+MQTTDISCONNECTED" 的子串 */
    return ESP8266_Exec("+MQTTCONNECTED", ESP8266_MQTT_TIMEOUT);
}

/**
 * @brief  发布一条消息, 见 esp8266.h
 */
ESP8266_Status ESP8266_MQTT_Publish(const char topic[], const char data[])
{
    /* AT+MQTTPUB=0,"<主题>","<载荷>",0,0 —— 载荷里的 " 和 , 得是已经转义好的
     * \" 和 \, (见 app_json.c): 模块按逗号切参数, 不转义会把 JSON 切碎 */
    (void)snprintf(s_Cmd, sizeof(s_Cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n",
                   topic, data);

    return ESP8266_Exec("OK", ESP8266_TIMEOUT);
}

/* ================================ 内部实现 ================================ */

/* --------------------------- 接收: DMA 环形缓冲 --------------------------- */

/* DMA 在 CIRCULAR 模式下自己搬字节, 我们只轮询 CNDTR 把新字节搬进解析缓冲.
 * 非用 DMA 不可: F1 的串口只有 1 字节接收寄存器, 而 DHT11 要关中断约 4ms,
 * 用 RXNE 中断收的话那 4ms 里的字节必丢 */

/* 启动 DMA 接收. Init 里调 (每次 Init 都要重挂) */
static ESP8266_Status ESP8266_RxStart(void)
{
    /* 先停再开: Receive_DMA 要求 RxState == READY, 而重连时 DMA 还在跑 */
    (void)HAL_UART_DMAStop(&ESP8266_UART);

    s_DmaTail = 0;

    if (HAL_UART_Receive_DMA(&ESP8266_UART, s_DmaBuf,
                             ESP8266_DMA_BUF_SIZE) != HAL_OK)
    {
        return ESP8266_ERR_AT;
    }

    /* 关掉 DMA 传输完成中断: 环形模式下 HAL 的回调什么都不做, 我们轮询 CNDTR */
    __HAL_DMA_DISABLE_IT(ESP8266_UART.hdmarx, DMA_IT_TC);

    /* 串口错误中断也要关(HAL 在 Receive_DMA 里顺手开的 CR3.EIE): 一开, 帧错/
     * 噪声/溢出都会被 HAL_UART_IRQHandler 当致命错误、直接中止 DMA 接收 */
    __HAL_UART_DISABLE_IT(&ESP8266_UART, UART_IT_ERR);

    return ESP8266_OK;
}

/* DMA 当前的写指针: 环形模式下 "缓冲区大小 - CNDTR" 恒等于写位置 */
static uint16_t ESP8266_RxHead(void)
{
    return (uint16_t)(ESP8266_DMA_BUF_SIZE -
                      __HAL_DMA_GET_COUNTER(ESP8266_UART.hdmarx));
}

/* 从环形缓冲取最多 max 字节到 dst, 返回实际取了多少(0 = 没新数据) */
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

/* 把新字节搬进解析缓冲. 放不下就少搬点: 读指针卡住就追不上 DMA 了 */
static void ESP8266_RxDrain(void)
{
    uint16_t room = (uint16_t)(ESP8266_RX_BUF_SIZE - 1U - s_RxLen);

    s_RxLen = (uint16_t)(s_RxLen + ESP8266_RxPop(&s_RxBuf[s_RxLen], room));
}

/* 清接收缓冲: DMA 里没搬走的和已搬进解析缓冲的都要清, 只清后者会被灌回来.
 * memset 不只是清内容: 靠它保证末尾恒为 '\0', strstr 才安全 */
static void ESP8266_ClearRxBuf(void)
{
    s_DmaTail = ESP8266_RxHead();
    memset(s_RxBuf, 0, sizeof(s_RxBuf));
    s_RxLen = 0;
}

/* -------------------------------- 命令收发 -------------------------------- */

/* 发 s_Cmd 里拼好的命令并等应答, 超时或模块回 ERROR/FAIL 就带着原因返回 */
static ESP8266_Status ESP8266_Exec(const char ack[], uint32_t timeoutMs)
{
    uint16_t len = (uint16_t)strlen(s_Cmd);
    uint32_t tick;

    /* 超过 256 字节就不发: 半条命令发过去只会回一句含义模糊的 ERROR */
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

    /* 判"发完没有"只能看 gState: 接收 DMA 让 RxState 一直是 BUSY_RX */
    tick = HAL_GetTick();
    while (ESP8266_UART.gState != HAL_UART_STATE_READY)
    {
        if ((HAL_GetTick() - tick) > ESP8266_TX_TIMEOUT)
        {
            return ESP8266_ERR_TIMEOUT;
        }
        BSP_DelayMs(1);
    }

    /* 顺序不能反: 先看应答, 再看 ERROR/FAIL, 最后才判超时 */
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

/* 发一条字面量命令. 带用户数据的先把命令拼进 s_Cmd, 再直接调 Exec */
static ESP8266_Status ESP8266_SendCmd(const char cmd[], const char ack[], uint32_t timeoutMs)
{
    (void)strcpy(s_Cmd, cmd);

    return ESP8266_Exec(ack, timeoutMs);
}
