/* DS1302 三线制 (CE/SCLK/IO), 非 I2C; LSB 先出, 上升沿写、下降沿读.
 * 引脚 PA7=CE, PB1=SCLK, PB0=IO; 写 IO 输出, 读 IO 输入(上拉). */

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

/* 解析 __DATE__/__TIME__ 到 DS1302_Time[], 作上电默认时间 */
static void DS1302_LoadCompileTime(void)
{
	const char *d = __DATE__;
	const char *t = __TIME__;

	/* 月份: 首字母区分 */
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

	/* 日 (两位) */
	DS1302_Time[2] = 0;
	if (d[4] >= '0' && d[4] <= '9') DS1302_Time[2] += (unsigned char)((d[4]-'0')*10);
	if (d[5] >= '0' && d[5] <= '9') DS1302_Time[2] += (unsigned char)(d[5]-'0');

	/* 年: 取后两位 */
	DS1302_Time[0] = (unsigned char)((d[9]-'0')*10 + (d[10]-'0'));

	/* 时:分:秒 */
	DS1302_Time[3] = (unsigned char)((t[0]-'0')*10 + (t[1]-'0'));
	DS1302_Time[4] = (unsigned char)((t[3]-'0')*10 + (t[4]-'0'));
	DS1302_Time[5] = (unsigned char)((t[6]-'0')*10 + (t[7]-'0'));

	DS1302_Time[6] = 1;  /* 星期不显示 */
}

/**
  * @brief  DS1302 初始化
  */
void DS1302_Init(void)
{
	/* 总线空闲: CE/SCLK/IO 置低 */
	DS1302_CE_L();
	DS1302_SCLK_L();
	DS1302_IoOut();
	DS1302_IO_L();

	/* 写入编译时间; 秒写 0 清 CH 位启动振荡器 */
	DS1302_LoadCompileTime();
	DS1302_SetTime();
}

/**
  * @brief  DS1302 写一字节
  * @param  Command 命令/地址
  * @param  Data 写入数据
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
  * @brief  DS1302 读一字节
  * @param  Command 命令/地址
  * @retval 读出数据
  */
unsigned char DS1302_ReadByte(unsigned char Command)
{
	unsigned char i,Data=0x00;
	Command|=0x01U;	/* 转为读命令 */
	DS1302_CE_H();
	/* 先发命令: IO 输出 */
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
	/* 再收数据: IO 切输入, 下降沿后输出该位 */
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
  * @brief  把 DS1302_Time[] 写入 DS1302 (转 BCD)
  */
void DS1302_SetTime(void)
{
	DS1302_WriteByte(DS1302_WP,0x00);
	DS1302_WriteByte(DS1302_YEAR,DS1302_Time[0]/10*16+DS1302_Time[0]%10);/* 十进制转 BCD */
	DS1302_WriteByte(DS1302_MONTH,DS1302_Time[1]/10*16+DS1302_Time[1]%10);
	DS1302_WriteByte(DS1302_DATE,DS1302_Time[2]/10*16+DS1302_Time[2]%10);
	DS1302_WriteByte(DS1302_HOUR,DS1302_Time[3]/10*16+DS1302_Time[3]%10);
	DS1302_WriteByte(DS1302_MINUTE,DS1302_Time[4]/10*16+DS1302_Time[4]%10);
	DS1302_WriteByte(DS1302_SECOND,DS1302_Time[5]/10*16+DS1302_Time[5]%10);
	DS1302_WriteByte(DS1302_DAY,DS1302_Time[6]/10*16+DS1302_Time[6]%10);
	DS1302_WriteByte(DS1302_WP,0x80);
}

/**
  * @brief  读 DS1302 到 DS1302_Time[] (BCD→十进制)
  */
void DS1302_ReadTime(void)
{
	unsigned char Temp;
	Temp=DS1302_ReadByte(DS1302_YEAR);
	DS1302_Time[0]=Temp/16*10+Temp%16;/* BCD 转十进制 */
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
