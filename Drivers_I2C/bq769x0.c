#include "main.h" //为了包含HAL库函数
#include "MyI2C.h"
#include "bq769x0.h"

#include <math.h>
#include <stdbool.h>//对布尔变量的定义（true false）

#define DBG_TAG "bq76920"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"


// 报警回调接口
static BQ769X0_AlertOpsTypedef AlertOps;

/* ADC增益 */
static float Gain = 0;//浮点形式的增益
static int16_t iGain = 0;//整数形式的增益
static int8_t Adcoffset;//ADC偏移量

static uint8_t TempSampleMode = 0;  // 温度采样模式 0:热敏电阻  1:IC温度

// 传感数据
BQ769X0_SampleDataTypedef BQ769X0_SampleData = {0};

//测量的电流的电阻值,单位是mΩ
static float RsnsValue=5;

// 寄存器组
static RegisterGroup Registers = {0};

// 18650 1C放电倍率是指电池以1小时时间放完额定容量
// 假如电池额定容量为2200mAh ,那么1C放电电流是2.2A
// 18650短路电流阈值一般为电池的5C放电速率
// 短路电流一般设置为10A（搜索别人实际测过的经验值）




// 放电短路保护阈值，选择44还是89是根据RSNS位配置的，为1则加倍阈值否则不加倍
// 假如SCD预设为10A 根据我的电路分流电阻规格是5mΩ
// 计算  	SCDThresh = 10A * 5mΩ = 50mV
// 向下取值应该取 SCD_THRESH_89mV_44mV  并且RSNS设置为0
// 取44mV 实际计算 44 / 5 = 8.8A 这才是真正的阈值保护电流
// 设置为56mV 放电短路阈值电流是11.2A
static const uint8_t SCDThresh = SCD_THRESH_89mV_44mV;

// 放电过流保护阈值，一般设置为2A
// 选择11还是22是根据RSNS位配置的，为1则加倍阈值否则不加倍
// 假如OCD预设为2.2A 根据我的电路分流电阻规格是5mΩ
// 计算  	OCDThresh = 2.2A * 5mΩ = 11mV
// 取值应该取 OCD_THRESH_22mV_11mV  并且RSNS设置为0
// 设置为8mV 放电过流阈值电流是1.6A
// 设置为14mV 放电过流阈值电流是2.8A
static const uint8_t OCDThresh = OCD_THRESH_22mV_11mV;

/* 三元锂和磷酸铁锂区别看标称电压,三元锂:3.6V或3.7V						磷酸铁锂:3.2V
// 三元锂过充终止:4.2V  				磷酸铁锂过充终止:3.6V
// 三元锂过放截止:3.2V  				磷酸铁锂过放截止:2.5V
// 以上值只是行业参考，锂电池过充、过放值还得根据电池卖家给的值

// BQ芯片可配置过压保护阈值,范围3.15~4.7V
static const uint16_t  OVPThreshold = 4200;		

// BQ芯片可配置欠压保护阈值,范围1.58~3.1V
static const uint16_t	UVPThreshold = 2900;
*/

//GPIO初始化，主要初始化的是唤醒BQ76920的GPIO口

static void BQ769X0_WAKEUP_SetOutMode(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	
	GPIO_InitStruct.Mode=GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pin=WAKEUP_Pin;
	GPIO_InitStruct.Pull=GPIO_NOPULL;
	GPIO_InitStruct.Speed=GPIO_SPEED_FREQ_HIGH;
	
	HAL_GPIO_Init(WAKEUP_GPIO_Port, &GPIO_InitStruct);
}

static void BQ769X0_WAKEUP_SetInMode(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	
	GPIO_InitStruct.Mode=GPIO_MODE_INPUT;
	GPIO_InitStruct.Pin=WAKEUP_Pin;
	GPIO_InitStruct.Pull=GPIO_NOPULL;
	GPIO_InitStruct.Speed=GPIO_SPEED_FREQ_HIGH;
	
	HAL_GPIO_Init(WAKEUP_GPIO_Port, &GPIO_InitStruct);
}
// CRC8校验
/*CRC 校验的本质是：将数据视为一个二进制多项式，除以一个预设的 
CRC 校验值。接收方用同样的生成多项式计算数据的 CRC，若与发送方的校验值一致，则数据大概率未被篡改。*/
static uint8_t CRC8(uint8_t *ptr, uint8_t len, uint8_t key)
{
	uint8_t i, crc=0;
	
	while (len-- != 0)
	{
		for (i = 0x80; i != 0; i /= 2)
		{
			if ((crc & 0x80) != 0)//如果crc的最高位是1
			{
				crc *= 2;//左移一位
				crc ^= key;//^表示异或，相同则为0，不同则为1
			}
			else
			{
				crc *= 2;
			}

			if ((*ptr & i) != 0)
			{
				crc ^= key;
			}
		}
		ptr++;
	}
	return(crc);
}

// 热敏电阻阻值换算成温度
static float TempChange(float	Rt)
{
	float temp = 0;

	// 热敏电阻在T2常温下的标称阻值,我买的是10K
	float Rp = 10000;

	// 该热敏电阻在开尔文温度下的,热敏电阻阻值为10K时对应的温度为25度
	float T2 = 273.15 + 25;

	// B值:3935、3950
	float Bx = 3950;

	// 开尔文温度值
	float Ka = 273.15;

	// 打印出热敏电阻的实时阻值可与购买链接的阻值与温度对应表对照查看
	//sprintf((char *)buffer, "%f", Rt);
	//BQ769X0_INFO("Rts value:%s", buffer);

	temp = 1 / (1 / T2 + log(Rt / Rp) / Bx)- Ka + 0.5;

	return temp;
}

//ALERT外部中断回调函数
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if(GPIO_Pin==ALERT_Pin)//如果确实是ALERT引脚触发的，调用回调函数
	{
		BQ769X0_AlertyHandler();
	}
}

/********************************** write and read ****************************************/

static bool BQ769X0_WriteRegisterByte(uint8_t Register, uint8_t data)//向寄存器写一个字节
{
	struct I2C_MessageTypeDef msg={0};//初始化一下msg
	uint8_t buffer[2]={Register,data};
	
	msg.flags=I2C_WR;
	msg.buf=buffer;
	msg.tLen=2;
	msg.addr=bq7692003PWR_I2C_ADDR;//查手册找到设备的地址，0x80，宏定义了下
	
	if(I2C_TransferMessages(&i2c1,&msg,1)!=1)//我们只需要传输一个信息,如果没返回1说明传输失败了
	{
		LOG_E("Write Register Byte Fail");
		
		return false;
	}
	return true;
	
}

static bool BQ769X0_WriteRegisterByte_CRC(uint8_t Register, uint8_t data)//使用CRC校验
{
	struct I2C_MessageTypeDef msg={0};//初始化一下msg
	uint8_t buffer[4];
	
	buffer[0]=bq7692003PWR_I2C_ADDR<<1;//校验设备地址
	buffer[1]=Register;
	buffer[2]=data;
	buffer[3]=CRC8(buffer,3,CRC_KEY);//BQ76920规定了前三个数据要一起进行CRC检验
	
	msg.flags=I2C_WR;
	msg.buf=buffer+1;//第一个设备地址信息不传进去，因为I2C_TransferMessages有传输设备地址的函数了
	msg.tLen=3;
	msg.addr=bq7692003PWR_I2C_ADDR;
	
	
	if(I2C_TransferMessages(&i2c1,&msg,1)!=1)//我们只需要传输一个信息,如果没返回1说明传输失败了
	{
		LOG_E("Write Register Byte Fail");
		
		return false;
	}
	return true;
}

static bool BQ769X0_WriteRegisterWord_CRC(uint8_t Register, uint16_t data)//向寄存器写入一个字
{
	struct I2C_MessageTypeDef msg={0};//初始化一下msg
	uint8_t buffer[6];
	
	buffer[0]=bq7692003PWR_I2C_ADDR<<1;//校验设备地址
	buffer[1]=Register;
	buffer[2]=Low_Byte(data);
	buffer[3]=CRC8(buffer,3,CRC_KEY);//BQ76920规定了前三个数据要一起进行CRC检验
	buffer[4]=High_Byte(data);
	buffer[5]=CRC8(buffer+4,1,CRC_KEY);//CRC检验高八位数据
	
	msg.flags=I2C_WR;
	msg.buf=buffer+1;//第一个设备地址信息不传进去，因为I2C_TransferMessages有传输设备地址的函数了
	msg.tLen=5;
	msg.addr=bq7692003PWR_I2C_ADDR;
	
	
	if(I2C_TransferMessages(&i2c1,&msg,1)!=1)
	{
		LOG_E("Write Register Byte Fail");
		
		return false;
	}
	return true;

}

static bool BQ769X0_WriteRegisterBlock_CRC(uint8_t Register, uint8_t *data,uint8_t length)//向寄存器连续写入几个字的数据
{																																					//length是我们需要发送的字节有几个
	uint8_t index;
	struct I2C_MessageTypeDef msg={0};
	uint8_t buffer[32],* p;//创建buffer用于存储数据,不像上面几个函数我们确定发送一个字或者一个字节，
	//这个函数我们不确定发送多少个字节，所以创建一个足够长的（32应该够用）数组存储数据，指针p协助存储数据
	
	p=buffer;
	*p++=bq7692003PWR_I2C_ADDR<<1;//第一个数据任然是从机设备地址，p++＝是先赋值然后指针++,也就是完成了buffer[0]=bq7692003PWR_I2C_ADDR<<1，然后p自己加一
	*p++=Register;
	*p++=*data;//取出data第一个数据给buffer
	*p=CRC8(buffer,3,CRC_KEY);//依旧是校验前三位
	
	for(index=1;index<length;index++)
	{
		p++;
		data++;
		*p=*data;//存放数据
		*(p+1)=CRC8(p,1,CRC_KEY);//buffer的下一位存储数据的CRC校验结果,这里直接输入p就行，因为是存放数据的那个指针
		p++;
	}
	
	msg.addr = bq7692003PWR_I2C_ADDR;
	msg.flags = I2C_WR;	
	msg.buf = buffer + 1;
	msg.tLen = 2 * length + 1;//目标发送数据长度符合这个算式
    if (I2C_TransferMessages(&i2c1, &msg, 1) != 1)
    {
		LOG_E("Write Register Block With CRC Fail");
		return false;
    }

    return true;	
}

//开始读寄存器函数的封装
static bool BQ769X0_ReadRegisterByte(uint8_t Register, uint8_t *data)//指定地址读一个字节
{
	struct I2C_MessageTypeDef msg[2]={0};
	
	msg[0].addr=bq7692003PWR_I2C_ADDR;
	msg[0].flags=I2C_WR;
	msg[0].buf=&Register;
	msg[0].tLen=1;//先把目标地址发过去
	
	msg[1].addr=bq7692003PWR_I2C_ADDR;//重复起始，依旧需要发送从机设备地址
	msg[1].flags=I2C_RD;//读模式
	msg[1].buf=data;//数据存放在data里面
	msg[1].tLen=1;//只需要接收一个字节
	
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Byte Fail");
		return false;
	}
	return true;
}

static bool BQ769X0_ReadRegisterByte_CRC(uint8_t Register, uint8_t *data)//指定地址读一个字节带CRC校验
{														//data既然我们8位的指针，我们一般就看待成普通指针，使用数组就写成data[]，这样就不会让读者误解
	struct I2C_MessageTypeDef msg[2]={0};
	uint8_t CRCInput[2],CRCValue;
	uint8_t CRCRead[2];
	
	msg[0].addr=bq7692003PWR_I2C_ADDR;
	msg[0].flags=I2C_WR;
	msg[0].buf=&Register;
	msg[0].tLen=1;//先把目标地址发过去
	
	msg[1].addr=bq7692003PWR_I2C_ADDR;//重复起始，依旧需要发送从机设备地址
	msg[1].flags=I2C_RD;//读模式
	msg[1].buf=CRCRead;//数据存放在CRCRead里面
	msg[1].tLen=2;//需要两个数据，一个是接受的字节，一个是CRC校验的结果
	
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Byte Fail");
		return false;
	}
	
	CRCInput[0]=(bq7692003PWR_I2C_ADDR<<1)|1;//查手册可以知道，指定地址读CRC返回的是设备地址和数据的CRC
																					//或上1是因为这里是读的地址，最后一位要是1
	CRCInput[1]=CRCRead[0];
	
	CRCValue=CRC8(CRCInput,2,CRC_KEY);
	
	if(CRCValue!=CRCRead[1])//如果我们算出来的CRC结果不是接收到的CRC数据
	{
		LOG_E("Read Register Byte CRC Check Fail");
		return false;
	}
	*data=CRCRead[0];//把数据存储到data里面，
	/*不使用中间变量CRCRead，那我们就得传进来data[]数组，不然会导致内存越界。因为我们只定义了一个字节的地址
	不像数组我们安排好了内存用于存储，如果我们msg[1].buf=data指向了data地址，虽然是在连续的
	地址存入了两个数据，但是第二个数据存入的地址我们没有定义，比如data地址是0x20000000存入了第一个数据
	然后0x20000001即将存入CRC校验的数据，但是由于我们没有分配这个地址，很有可能这个地址是其他的
	数据，然后我们把他覆盖掉了，这就是内存越界。而data[2]我们就分配好了这两个地址，不会导致越界
	同理，我们使用中间变量也是这个道理，防止出现内存越界
	上面向寄存器写字节那些函数也是先定义好了数组然后传输，而不是只是定义了一个地址然后向地址后面
	连续传输数据*/
	return true;
}

static bool BQ769X0_ReadRegisterWord_CRC(uint8_t Register, uint16_t *data)//指定地址读一个字
{
	struct I2C_MessageTypeDef msg[2]={0};
	uint8_t CRCInput[4],CRCValue;
	uint8_t CRCRead[4];
	
	msg[0].addr=bq7692003PWR_I2C_ADDR;
	msg[0].flags=I2C_WR;
	msg[0].buf=&Register;
	msg[0].tLen=1;//先把目标地址发过去
	
	msg[1].addr=bq7692003PWR_I2C_ADDR;//重复起始，依旧需要发送从机设备地址
	msg[1].flags=I2C_RD;//读模式
	msg[1].buf=CRCRead;//数据存放在CRCRead里面,需要读低八位和高八位数据以及对应的CRC校验结果
	msg[1].tLen=4;//需要接受4个字节
	
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Byte Fail");
		return false;
	}
	
	CRCInput[0]=(bq7692003PWR_I2C_ADDR<<1)|1;
	CRCInput[1]=CRCRead[0];//低八位的数据
	CRCValue=CRC8(CRCInput,2,CRC_KEY);//低八位数据与发送地址的校验
	if (CRCValue != CRCRead[1])
	{
		LOG_E("Read Register Word CRC 1 Check Fail");

		return false;
	}
	
	CRCInput[2]=CRCRead[2];//高八位的数据
	CRCValue=CRC8(CRCInput+2,1,CRC_KEY);//高八位数据的校验
	if (CRCValue != CRCRead[3])
	{
		LOG_E("Read Register Word CRC 2 Check Fail");

		return false;
	}
	
	*data=(CRCRead[2]<<8)|CRCRead[0];//把十六位数据存进data
	
	return true;
}

static bool BQ769X0_ReadBlockWithCRC(uint8_t startAddress, uint8_t *buffer, uint8_t length)//连续读几个寄存器
{
	struct I2C_MessageTypeDef msg[2]={0};
	uint8_t CRCInput[2],CRCValue;
	uint8_t CRCRead[32],*p;
	uint8_t temp;
	p=CRCRead;
	
	msg[0].addr=bq7692003PWR_I2C_ADDR;
	msg[0].flags=I2C_WR;
	msg[0].buf=&startAddress;
	msg[0].tLen=1;//先把目标地址发过去
		
	msg[1].addr=bq7692003PWR_I2C_ADDR;//重复起始，依旧需要发送从机设备地址
	msg[1].flags=I2C_RD;//读模式
	msg[1].buf=CRCRead;//数据存放在CRCRead里面
	msg[1].tLen=2*length;
	
	if (I2C_TransferMessages(&i2c1, msg, 2) != 2)
	{
		LOG_E("Read Register Byte Fail");
		return false;
	}
	
	CRCInput[0]=(bq7692003PWR_I2C_ADDR<<1)|1;
	CRCInput[1]=CRCRead[0];
	
	CRCValue=CRC8(CRCInput,2,CRC_KEY);

	if(CRCValue!=CRCRead[1])
	{
		LOG_E("Read Register frist CRC Check Fail");
		return false;
	}
	*buffer=CRCRead[0];
	 buffer++;
	
	p=p+2;//指针加2，现在p就是CRCRead[2]
	for(temp=1;temp<length;temp++)//之后的数据只需要检验一次
	{
		CRCValue=CRC8(p,1,CRC_KEY);//CRCRead[2]存放的是第二个数据
		p++;//CRCRead[3]存放的是第二个数据的CRC校验
		if(CRCValue!=*p)
		{
			LOG_E("Read Register frist CRC Check Fail");
			return false;
		}
		*buffer=*(p-1);//存入数据
		buffer++;
		p++;//每个循环都实现了指针加2
	}

	return true;
}


/********************************** 芯片初始化和寄存器控制****************************************/

// 报警处理
//我们读SYS_STAT寄存器可以读取各个状态的报警标志位
//SYS_STAT寄存器比较特殊，只有我们主机往相应位写1才可以清除标志位，详细可以查手册
static void BQ769X0_AlertyHandler(void)
{
	uint8_t reg_value=0,write_value=0;
	
	BQ769X0_ReadRegisterByte_CRC(SYS_STAT,&reg_value);//寄存器值读到reg_value里
	
	if(reg_value & SYS_STAT_OCD_BIT)//SYS_STAT_OCD_BIT自己宏定义了下，就是寄存第一位置1的情况。如果是OCD报警
	{
		write_value |= SYS_STAT_OCD_BIT;//这一步是为了为清除标志位做准备，或上标志位，以后每次都做这个操作，就可以相应报警同时清除相应报警
		if(AlertOps.ocd!=NULL)//如果函数非空
			AlertOps.ocd();//执行报警函数
	}
	
	if (reg_value & SYS_STAT_SCD_BIT)
	{
		write_value |= SYS_STAT_SCD_BIT;
		if (AlertOps.scd != NULL) 
			AlertOps.scd();
	}

	if (reg_value & SYS_STAT_OV_BIT)
	{
		write_value |= SYS_STAT_OV_BIT;
		if (AlertOps.ov != NULL)
			AlertOps.ov();
	}	

	if (reg_value & SYS_STAT_UV_BIT)
	{
		write_value |= SYS_STAT_UV_BIT;
		if (AlertOps.uv != NULL) 
			AlertOps.uv();
	}

	if (reg_value & SYS_STAT_OVRD_BIT)
	{
		write_value |= SYS_STAT_OVRD_BIT;
		if (AlertOps.ovrd != NULL) 
			AlertOps.ovrd();
	}	

	if (reg_value & SYS_STAT_DEVICE_BIT)
	{
		write_value |= SYS_STAT_DEVICE_BIT;
		if (AlertOps.device != NULL) 
			AlertOps.device();
	}
	
	if (reg_value & SYS_STAT_CC_BIT)
	{
		write_value |= SYS_STAT_CC_BIT;
		if (AlertOps.cc != NULL) 
		{
			AlertOps.cc();
		}
	}		

	BQ769X0_WriteRegisterByte_CRC(SYS_STAT, write_value);//清空寄存器的值
}

//获取增益（gain）和偏移量（offset）,手册40页
static void BQ769X0_GetGainOffset(void)
{
	//ADCGAIN1,ADCGAIN2的地址不是连续的，所以我们只能分开一个一个读了
	//寄存器的地址宏定义和存放寄存器对应值的结构体在bq760x0.h定义好了
	BQ769X0_ReadRegisterByte_CRC(ADCGAIN1,&(Registers.ADCGain1.ADCGain1Byte));
	BQ769X0_ReadRegisterByte_CRC(ADCGAIN2,&(Registers.ADCGain2.ADCGain2Byte));
	BQ769X0_ReadRegisterByte_CRC(ADCOFFSET,&(Registers.ADCOffset));
	
	//读出的数据还得进行处理，查手册的相应寄存器定义，我们得对增益进行处理
	//ADCGAIN1的2，3(第三第四个位)位读出的是增益的高两位，ADCGAIN2的5，6，7(第六、七、八)位读出的是增益的低三位
	//ADCGAIN_BASE是增益的基数，手册里面定义为365
	//位运算的>>优先级要高于&操作，我们得加括号
	Gain=(ADCGAIN_BASE+(((Registers.ADCGain1.ADCGain1Byte&0x0C)<<1)+((Registers.ADCGain2.ADCGain2Byte&0xE0)>>5)))/1000.0;//转化为浮点数，并且单位转化为mv
	iGain=ADCGAIN_BASE+(((Registers.ADCGain1.ADCGain1Byte&0x0C)<<1)+((Registers.ADCGain2.ADCGain2Byte&0xE0)>>5));
	
	//OFFSET的值也要进行相应的处理，在小于0x7F为正,0x7F是127，0x80是-128，是0xFF就是-1
	if(Registers.ADCOffset<=0x7F)
	{
		Adcoffset=Registers.ADCOffset;
	}
	else
	{
		Adcoffset=Registers.ADCOffset-256;
	}
}

//配置寄存器
static void BQ769X0_Configuration(void)
{
	//uint8_t* test;
	
	/*要使能ADC，必须设置SYS_CTRL1寄存器中的[ADC_EN]位。手册20页
	每当器件进入NORMAL模式时，该位会自动设置。*/
	// 开ADC,选择外部NTC
	Registers.SysCtrl1.SysCtrl1Byte = 0x18;
	
	// 使能电流连续采样，关闭充放电MOS
	Registers.SysCtrl2.SysCtrl2Byte = 0x40;
	
	// 配置CC_CFG,说明书要求在初始化时应配置为0X19以获得更好的性能
	Registers.CCCfg = 0x19;
	
	BQ769X0_WriteRegisterBlock_CRC(SYS_CTRL1,&(Registers.SysCtrl1.SysCtrl1Byte),8);
	
	//BQ769X0_ReadRegisterByte_CRC(SYS_CTRL1,test);//当时找I2C问题测试的时候用的
	//rt_kprintf("%d",*test);
}

//负载检测
bool BQ769X0_LoadDetect(void)
{
	BQ769X0_ReadRegisterWord_CRC(SYS_CTRL1,(uint16_t*)&(Registers.SysCtrl1.SysCtrl1Byte));//连续读SYS_CTRL1和SYS_CTRL2
	if(Registers.SysCtrl2.SysCtrl2Bit.CHG_ON==0)//必须在抑制充电情况
	{
		if(Registers.SysCtrl1.SysCtrl1Bit.LOAD_PRESENT==1)//LOAD_PRESENT位置1说明有外部负载
		{
			return true;
		}
	}
	return false;
}

//唤醒BQ76920芯片
//使用我们的WAKE_UP引脚
void BQ769X0_Wakeup(void)
{
	BQ769X0_WAKEUP_SetOutMode();//引脚调成输出模式
	HAL_GPIO_WritePin(WAKEUP_GPIO_Port,WAKEUP_Pin,GPIO_PIN_SET);
	BQ769X0_DELAY(1000);
	
	// 设为输入模式，避免干扰温度采样
	BQ769X0_WAKEUP_SetInMode();
	HAL_GPIO_WritePin(WAKEUP_GPIO_Port,WAKEUP_Pin,GPIO_PIN_RESET);
	BQ769X0_DELAY(1000);
}

//进入低功耗模式
//手册里面说明了必须以这个顺序写才可以进入低功耗模式
void BQ769X0_EntryShip(void)
{
	BQ769X0_WriteRegisterByte_CRC(SYS_CTRL1, 0x00);
	BQ769X0_WriteRegisterByte_CRC(SYS_CTRL1, 0x01);
	BQ769X0_WriteRegisterByte_CRC(SYS_CTRL1, 0x02);
}

//充放电控制
void BQ769X0_ControlDSGOrCHG(BQ769X0_StateTypedef state,BQ769X0_ControlTypedef control)
{
	if(state==BQ_STATE_ENABLE)
	{
		Registers.SysCtrl2.SysCtrl2Byte|=control;//CHG_CONTROL = 0x01，充电，第一位置1第二位置0
	}
	else//禁用状态
	{
		Registers.SysCtrl2.SysCtrl2Byte &= ~ control;//想置0就与0，想置1就或1，这个操作就是如果想禁用
																								//比如禁用充电，那就第一位与0，其他位置一就不会改变，~是取反
	}
	BQ769X0_WriteRegisterByte_CRC(SYS_CTRL2, Registers.SysCtrl2.SysCtrl2Byte);
	LOG_I("Write Successful");
}

//电池均衡设置
void BQ769X0_CellBalanceControl(BQ769X0_CellIndexTypedef CellIndex, BQ769X0_StateTypedef state)
{
		static uint8_t CELL_BAL_VALUE[3] = {0};

	if (state == BQ_STATE_ENABLE)
	{
		CELL_BAL_VALUE[0] |= CellIndex & 0x1F;
		CELL_BAL_VALUE[1] |= (CellIndex >> 5) & 0x1F;
		CELL_BAL_VALUE[2] |= (CellIndex >> 10) & 0x1F;
	}
	else if (state == BQ_STATE_DISABLE)
	{
		CELL_BAL_VALUE[0] &= ~(CellIndex & 0x1F);
		CELL_BAL_VALUE[1] &= ~((CellIndex >> 5) & 0x1F);
		CELL_BAL_VALUE[2] &= ~((CellIndex >> 10) & 0x1F);
	}
	BQ769X0_WriteRegisterBlock_CRC(CELLBAL1, CELL_BAL_VALUE, 3);
}

//BQ76920初始化
void BQ769X0_Init(BQ769X0_InitDataTypedef *InitData)//计划在应用层使用的时候初始化InitData
{
	// 进入睡眠再唤醒相当于复位一次BQ芯片
	BQ769X0_EntryShip();
	BQ769X0_DELAY(500);
	BQ769X0_Wakeup();
	
	BQ769X0_GetGainOffset();//获取偏移量
	
	AlertOps=InitData->AlertOps;//回调函数赋值，这个得用保护线程的函数进行赋值，现在硬件层我们不写
	
	Registers.Protect1.Protect1Bit.SCD_THRESH	=	SCDThresh;	//上面我们定义好了，其实也可以用枚举进行阈值的封装，只是这里用宏定义了
	Registers.Protect2.Protect2Bit.OCD_THRESH = OCDThresh;	//结构体其实更统一一点，这里就暂时不封装了
	Registers.Protect1.Protect1Bit.SCD_DELAY  = InitData->ConfigData.SCDDelay;//配置保护机制的延迟时间
	Registers.Protect2.Protect2Bit.OCD_DELAY  = InitData->ConfigData.OCDDelay;	
	Registers.Protect3.Protect3Bit.UV_DELAY   = InitData->ConfigData.UVDelay;	
	Registers.Protect3.Protect3Bit.OV_DELAY   = InitData->ConfigData.OVDelay;
	
	// BQ阈值寄存器内部比较是14位的，但我们真实写入的值是“10-XXXX-XXXX–1000”中间x的数据，所以下面计算出14位数据后需要得到中间8位再写入
  // 减去OV_THRESH_BASE就是得到中间8位，，在数据手册7.3.1.2.1章节有讲解
  Registers.OVTrip = (uint8_t)((((uint16_t)((InitData->ConfigData.OVPThreshold - Adcoffset)/Gain)/* - OV_THRESH_BASE*/) >> 4) & 0xFF);
  // BQ阈值寄存器内部比较是14位的，但我们真实写入的值是“01-XXXX-XXXX–0000”中间x的数据，所以下面计算出14位数据后需要得到中间8位再写入，
	//个人认为不需要减去基准OV_THRESH_BASE，后续记得验证准确性，但是写上逻辑意义更加明确
  // 减去UV_THRESH_BASE就是得到中间8位，在数据手册7.3.1.2.1章节有讲解
  Registers.UVTrip = (uint8_t)((((uint16_t)((InitData->ConfigData.UVPThreshold - Adcoffset)/Gain) - UV_THRESH_BASE) >> 4) & 0xFF);
	//计算里面的值单位全都是mv

	BQ769X0_Configuration();//配置寄存器，同时把SysCtrl1到CCCfg的数据全部配置好，8个地址的数据
	
	LOG_I("BQ769X0 Initialize successful!");
}


/********************************** 数据采集****************************************/

//更新电芯电压寄存器，寄存器里面的值肯定不是最终我们想要的电压值，还需要进行处理，不过这不是这个函数的任务
//具体的电压计算公式可以查手册看到：V(cell) = GAIN x ADC(cell) + OFFSET ，具体在手册21页
//GAIN是增益，OFFSET是偏移，14位ADC采样之后的值还得转化为数值进行最终计算
/* 更新单节电芯电压 250ms更新一次,手册上写明ADC每250ms更新五节电池的数据*/
void BQ769X0_UpdateCellVolt(void)
{
	uint8_t i=0;
	uint16_t VCellWord;
	uint8_t *temp;
	uint32_t Final_Val;
	//BQ769X0_CELL_MAX<<1相当于×2，因为我们要读10个数据（5节电池）
	if(BQ769X0_ReadBlockWithCRC(VC1_HI_BYTE,&(Registers.VCell1.VCell1Byte.VC1_HI),BQ769X0_CELL_MAX<<1)==false)
	{
		LOG_E("Update Cell Voltage Fail");
	}
	
	temp=&(Registers.VCell1.VCell1Byte.VC1_HI);
	//没有直接取十六位地址的原因是这东西高八位在前，那就说明连续两个地址先是高八位，再是低八位，不能直接取数据
	for(i=0;i<BQ769X0_CELL_MAX;i++)
	{
		VCellWord=(*temp << 8)|*(temp+1);//每一节电池的高八位和低八位合并
		Final_Val=(iGain*(unsigned long)VCellWord)/1000+Adcoffset;//iGain*VCellWord单位是uv，除1000转化为mv
		BQ769X0_SampleData.CellVoltage[i]=Final_Val/1000.0;//转化为浮点数，并且转化为v
		temp+=2;
	}
}

//获取ic内部温度,2s更新一次,测试的总是数值不对
void BQ769X0_UpdataDieTemperature(void)
{
	uint16_t TSWord;
	float TEMP,TSWord_V;
	if(BQ769X0_ReadRegisterWord_CRC(TS1_HI_BYTE	,&(Registers.TS1.TS1Word))==false)
	{
		LOG_E("Update Cell Temperature Fail");
	}
	TSWord=(uint16_t)(Registers.TS1.TS1Byte.TS1_HI<<8)|Registers.TS1.TS1Byte.TS1_LO;
	//根据手册的公式计算最后的温度，24页
	
	TSWord_V=TSWord*382/1000.0/1000.0;//温度转化为伏特
	TEMP=25-((TSWord_V-1.2)/0.0042);
	BQ769X0_SampleData.DieTemperature=TEMP;
	
}

/* 热敏电阻温度 2s更新一次 */
void BQ769X0_UpdateTsTemp(void)
{
	uint8_t index;
	uint16_t iTemp = 0;
	float v_tsx = 0;
	float Rts = 0;
	uint8_t *pRawADCData = NULL;

	
	if (TempSampleMode != 0)
	{
		TempSampleMode = 0;
		if (BQ769X0_WriteRegisterByte_CRC(SYS_CTRL1, 0x18) != true)//要置响应位1才能使用热敏电阻测温
	 	{
			LOG_E("Update Tsx Temperature Fail");
	 	}
		BQ769X0_DELAY(2000);
	}

	if (BQ769X0_ReadBlockWithCRC(TS1_HI_BYTE, &(Registers.TS1.TS1Byte.TS1_HI), BQ769X0_TMEP_MAX << 1) != true)
 	{
		LOG_E("Update Tsx Temperature Fail");
 	}

	pRawADCData = &Registers.TS1.TS1Byte.TS1_HI;

		// 读出ADC值
		iTemp = (uint16_t)(*pRawADCData << 8) | *(pRawADCData + 1);
		
		// 手册上公式是直接用Uv单位,在这我换成V单位
		v_tsx = iTemp * 0.000382;

		// Rts:热敏电阻阻值
		// 根据adc值算出热敏电阻阻值,单位:Ω
		Rts = (10000 * v_tsx) / (3.3 - v_tsx);

		// 根据电阻值算出对应的温度值
		BQ769X0_SampleData.TsxTemperature = TempChange(Rts);

}

//更新电流 
void BQ769X0_UpdateCurrent(void)
{
	int32_t CC;//这个是有符号的
	if(BQ769X0_ReadRegisterWord_CRC(CC_HI_BYTE,&(Registers.CC.CCWord))==false)
	{
		LOG_E("Update Cell Current Fail");
	}
	CC=Registers.CC.CCByte.CC_HI<<8|Registers.CC.CCByte.CC_LO;//读取库伦值
	//电流计算方法在手册23页
	
	if(CC&0x8000)//CC值小于0x8000，CC值是正的，大于就是负的，负数情况得转换一下
	{
		CC= -((~CC + 1) & 0xFFFF);
	}
	CC=CC*8.44;//计算出对应的模拟电压，这时候已经转化为浮点类型了，单位是uv，
	//计算结果最终得转化为A，uv/1000/1000,mΩ/1000
	BQ769X0_SampleData.BatteryCurrent=(CC/RsnsValue)*0.001;
}

/*
// 调试用：打印所有电池电压
void HAL_PrintString(UART_HandleTypeDef *huart, const char *str)
{
  // 发送字符串（长度为 strlen(str)，超时时间 100ms）
  HAL_UART_Transmit(huart, (uint8_t*)str, strlen(str), 100);
}

// 打印单节电池电压（float 类型）
void HAL_PrintCellVoltage(UART_HandleTypeDef *huart, uint8_t cellNum, float voltage)
{
  char str[32];  // 缓冲区，足够存储 "电池1: 3.210V\n" 这样的字符串
  
  // 用 sprintf 将浮点数格式化为字符串（保留3位小数）
  sprintf(str, "电池%d: %.3fV\n", cellNum, voltage);
  
  // 发送字符串到串口
  HAL_PrintString(huart, str);
}
*/