/* ============================================================================
 * DS1302 实时时钟驱动 —— STM32F1 + HAL
 *
 * ⚠️ 三线制, 不是 I2C: CE(RST) + SCLK + IO(双向数据) 三根线.
 *    LSB 先出; 命令/数据在 SCLK 上升沿被 DS1302 采样, 读回在下降沿由 DS1302 输出.
 *
 * 引脚(由 CubeMX 配好, 本文件不重配 CE/SCLK, 只按读写需要切 IO 方向):
 *    PA7 = CE/RST   (推挽输出)
 *    PB1 = SCLK     (推挽输出)
 *    PB0 = IO/DA    (双向: 写时输出, 读时输入)
 *
 * STM32 没有 8051 那种"写 1 即释放"的准双向口, 所以 IO 必须显式切方向:
 * 写的时候配成推挽输出去驱动, 读的时候配成输入(内部上拉)去采样.
 * ========================================================================== */

#include "DS1302.h"
#include "bsp_delay.h"

#define DS1302_HALF_US    2U            /* SCLK 半周期 ~2us, DS1302 上限 2MHz, 够慢也够稳 */

#define DS1302_CE_H()     HAL_GPIO_WritePin(DS1302_CE_PORT,   DS1302_CE_PIN,   GPIO_PIN_SET)
#define DS1302_CE_L()     HAL_GPIO_WritePin(DS1302_CE_PORT,   DS1302_CE_PIN,   GPIO_PIN_RESET)
#define DS1302_SCLK_H()   HAL_GPIO_WritePin(DS1302_SCLK_PORT, DS1302_SCLK_PIN, GPIO_PIN_SET)
#define DS1302_SCLK_L()   HAL_GPIO_WritePin(DS1302_SCLK_PORT, DS1302_SCLK_PIN, GPIO_PIN_RESET)
#define DS1302_IO_H()     HAL_GPIO_WritePin(DS1302_IO_PORT,   DS1302_IO_PIN,   GPIO_PIN_SET)
#define DS1302_IO_L()     HAL_GPIO_WritePin(DS1302_IO_PORT,   DS1302_IO_PIN,   GPIO_PIN_RESET)
#define DS1302_IO_IN()    ((DS1302_IO_PORT->IDR & DS1302_IO_PIN) ? 1U : 0U)

//寄存器写入地址/指令定义
#define DS1302_SECOND		0x80
#define DS1302_MINUTE		0x82
#define DS1302_HOUR			0x84
#define DS1302_DATE			0x86
#define DS1302_MONTH		0x88
#define DS1302_DAY			0x8A
#define DS1302_YEAR			0x8C
#define DS1302_WP			0x8E

/* IO 方向切换: 写 = 推挽输出, 读 = 输入(内部上拉, 空闲为高) */
static void DS1302_IoOut(void)
{
	GPIO_InitTypeDef s = {0};
	s.Pin   = DS1302_IO_PIN;
	s.Mode  = GPIO_MODE_OUTPUT_PP;
	s.Pull  = GPIO_NOPULL;
	s.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(DS1302_IO_PORT, &s);
}

static void DS1302_IoIn(void)
{
	GPIO_InitTypeDef s = {0};
	s.Pin   = DS1302_IO_PIN;
	s.Mode  = GPIO_MODE_INPUT;
	s.Pull  = GPIO_PULLUP;
	HAL_GPIO_Init(DS1302_IO_PORT, &s);
}

//时间数组，索引0~6分别为年、月、日、时、分、秒、星期
unsigned char DS1302_Time[]={26,10,15,12,0,0,4};

/* 把 __DATE__ ("Oct 15 2026") / __TIME__ ("14:30:00") 解析进 DS1302_Time[]
 * 用编译时间作为上电默认时间, 比写死的固定值更接近真实时间. */
static void DS1302_LoadCompileTime(void)
{
	const char *d = __DATE__;
	const char *t = __TIME__;

	/* 月份: 用首字母 + 第二字母快速区分, 避免大段 strcmp */
	if      (d[0]=='J' && d[1]=='a') DS1302_Time[1]=1;	/* Jan */
	else if (d[0]=='F')              DS1302_Time[1]=2;	/* Feb */
	else if (d[0]=='M' && d[2]=='r') DS1302_Time[1]=3;	/* Mar */
	else if (d[0]=='A' && d[1]=='p') DS1302_Time[1]=4;	/* Apr */
	else if (d[0]=='M' && d[2]=='y') DS1302_Time[1]=5;	/* May */
	else if (d[0]=='J' && d[2]=='n') DS1302_Time[1]=6;	/* Jun */
	else if (d[0]=='J' && d[2]=='l') DS1302_Time[1]=7;	/* Jul */
	else if (d[0]=='A' && d[1]=='u') DS1302_Time[1]=8;	/* Aug */
	else if (d[0]=='S')              DS1302_Time[1]=9;	/* Sep */
	else if (d[0]=='O')              DS1302_Time[1]=10;	/* Oct */
	else if (d[0]=='N')              DS1302_Time[1]=11;	/* Nov */
	else                              DS1302_Time[1]=12;	/* Dec */

	/* 日: __DATE__ 格式 "Mmm DD YYYY", 日为两位, 不足补空格 */
	DS1302_Time[2] = 0;
	if (d[4] >= '0' && d[4] <= '9') DS1302_Time[2] += (unsigned char)((d[4]-'0')*10);
	if (d[5] >= '0' && d[5] <= '9') DS1302_Time[2] += (unsigned char)(d[5]-'0');

	/* 年: 取后两位 */
	DS1302_Time[0] = (unsigned char)((d[9]-'0')*10 + (d[10]-'0'));

	/* 时:分:秒 */
	DS1302_Time[3] = (unsigned char)((t[0]-'0')*10 + (t[1]-'0'));
	DS1302_Time[4] = (unsigned char)((t[3]-'0')*10 + (t[4]-'0'));
	DS1302_Time[5] = (unsigned char)((t[6]-'0')*10 + (t[7]-'0'));

	DS1302_Time[6] = 1;  /* 星期不参与显示, 随便给个有效值 */
}

/**
  * @brief  DS1302初始化
  * @param  无
  * @retval 无
  */
void DS1302_Init(void)
{
	/* 总线空闲: CE 拉低, SCLK 拉低, IO 置低 */
	DS1302_CE_L();
	DS1302_SCLK_L();
	DS1302_IoOut();
	DS1302_IO_L();

	/* 用编译时间填入 DS1302_Time[], 然后无条件写入 DS1302.
	 * 这样做两件事: 1) 时间被对到编译时刻附近; 2) 秒寄存器写 0 会清掉 CH 位,
	 *    强制启动振荡器 —— 解决"秒数不跳"的问题. */
	DS1302_LoadCompileTime();
	DS1302_SetTime();
}

/**
  * @brief  DS1302写一个字节
  * @param  Command 命令字/地址
  * @param  Data 要写入的数据
  * @retval 无
  */
void DS1302_WriteByte(unsigned char Command, unsigned char Data)
{
	unsigned char i;
	DS1302_CE_H();
	DS1302_IoOut();
	for(i=0;i<8;i++)
	{
		if(Command&0x01U){DS1302_IO_H();}else{DS1302_IO_L();}
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_H();
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_L();
		Command>>=1;
	}
	for(i=0;i<8;i++)
	{
		if(Data&0x01U){DS1302_IO_H();}else{DS1302_IO_L();}
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_H();
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_L();
		Data>>=1;
	}
	DS1302_CE_L();
}

/**
  * @brief  DS1302读一个字节
  * @param  Command 命令字/地址
  * @retval 读出的数据
  */
unsigned char DS1302_ReadByte(unsigned char Command)
{
	unsigned char i,Data=0x00;
	Command|=0x01U;	//将指令转换为读指令
	DS1302_CE_H();
	/* 先发命令 (IO 输出) */
	DS1302_IoOut();
	for(i=0;i<8;i++)
	{
		if(Command&0x01U){DS1302_IO_H();}else{DS1302_IO_L();}
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_H();
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_L();
		Command>>=1;
	}
	/* 再收数据 (IO 切输入). 下降沿后 DS1302 才输出该位 */
	DS1302_IoIn();
	for(i=0;i<8;i++)
	{
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_H();
		BSP_DelayUs(DS1302_HALF_US);
		DS1302_SCLK_L();
		BSP_DelayUs(DS1302_HALF_US);
		if(DS1302_IO_IN()){Data|=(unsigned char)(0x01U<<i);}
	}
	DS1302_CE_L();
	DS1302_SCLK_L();
	return Data;
}

/**
  * @brief  DS1302设置时间，调用之后，DS1302_Time数组的数字会被设置到DS1302中
  * @param  无
  * @retval 无
  */
void DS1302_SetTime(void)
{
	DS1302_WriteByte(DS1302_WP,0x00);
	DS1302_WriteByte(DS1302_YEAR,DS1302_Time[0]/10*16+DS1302_Time[0]%10);//十进制转BCD码后写入
	DS1302_WriteByte(DS1302_MONTH,DS1302_Time[1]/10*16+DS1302_Time[1]%10);
	DS1302_WriteByte(DS1302_DATE,DS1302_Time[2]/10*16+DS1302_Time[2]%10);
	DS1302_WriteByte(DS1302_HOUR,DS1302_Time[3]/10*16+DS1302_Time[3]%10);
	DS1302_WriteByte(DS1302_MINUTE,DS1302_Time[4]/10*16+DS1302_Time[4]%10);
	DS1302_WriteByte(DS1302_SECOND,DS1302_Time[5]/10*16+DS1302_Time[5]%10);
	DS1302_WriteByte(DS1302_DAY,DS1302_Time[6]/10*16+DS1302_Time[6]%10);
	DS1302_WriteByte(DS1302_WP,0x80);
}

/**
  * @brief  DS1302读取时间，调用之后，DS1302中的数据会被读取到DS1302_Time数组中
  * @param  无
  * @retval 无
  */
void DS1302_ReadTime(void)
{
	unsigned char Temp;
	Temp=DS1302_ReadByte(DS1302_YEAR);
	DS1302_Time[0]=Temp/16*10+Temp%16;//BCD码转十进制后读取
	Temp=DS1302_ReadByte(DS1302_MONTH);
	DS1302_Time[1]=Temp/16*10+Temp%16;
	Temp=DS1302_ReadByte(DS1302_DATE);
	DS1302_Time[2]=Temp/16*10+Temp%16;
	Temp=DS1302_ReadByte(DS1302_HOUR);
	DS1302_Time[3]=Temp/16*10+Temp%16;
	Temp=DS1302_ReadByte(DS1302_MINUTE);
	DS1302_Time[4]=Temp/16*10+Temp%16;
	Temp=DS1302_ReadByte(DS1302_SECOND);
	DS1302_Time[5]=Temp/16*10+Temp%16;
	Temp=DS1302_ReadByte(DS1302_DAY);
	DS1302_Time[6]=Temp/16*10+Temp%16;
}
