#include "main.h"
#include "MyI2C.h"
#include <rtthread.h>
#include "rthw.h"

static rt_uint32_t level;


static void I2C1_LockInit(void)
{
    //rt_mutex_init(&mutex1, "i2c1_lock", RT_IPC_FLAG_FIFO);
		// 初始化互斥锁，名称为"i2c1_lock"，初始状态为解锁
}

static void I2C1_Lock(void)
{
    //rt_mutex_take(&mutex1, RT_WAITING_FOREVER);
		// 尝试获取互斥锁，最长等待时间设为RT_WAITING_FOREVER（永久等待，直到获取锁）
	level = rt_hw_interrupt_disable();//关闭中断，同时保存当前上下文
}

static void I2C1_Unlock(void)
{
    //rt_mutex_release(&mutex1);
		// 释放互斥锁
	rt_hw_interrupt_enable(level);//使能中断，开关中断是为了中断不打扰I2C的信息正常传递
}

static void delay_us(uint32_t us)
{
	uint16_t i = 0;
	
	while(us--)
	{
		i = 10; //自己定义
		while(i--);
	}
}


struct I2C_BusTypeDef i2c1=
	{
		.gpiox=SDA_GPIO_Port,
		.sda_gpio_pin = GPIO_PIN_13,
		.scl_gpio_pin = GPIO_PIN_14,
		.retries = 3,
		.udelay = delay_us,
		.lockInit = I2C1_LockInit,
		.lock = I2C1_Lock,
		.unlock = I2C1_Unlock,
	};

static void I2C_BusHardwareInitialize(struct I2C_BusTypeDef *bus)
{
	GPIO_InitTypeDef GPIO_InitStruct;

    GPIO_InitStruct.Pin = bus->scl_gpio_pin | bus->sda_gpio_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    
    HAL_GPIO_Init(bus->gpiox, &GPIO_InitStruct);

    HAL_GPIO_WritePin(bus->gpiox, bus->scl_gpio_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(bus->gpiox, bus->sda_gpio_pin, GPIO_PIN_SET);
	
    if (bus->lockInit)bus->lockInit();
}

static inline void SCL_Set(uint8_t num)//小型、简单、被频繁调用的函数使用 inline
{
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_14,(GPIO_PinState)num);
	delay_us(1);
}


static inline void SDA_Set(uint8_t num)
{
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_13,(GPIO_PinState)num);
	delay_us(1);
}

static inline GPIO_PinState SDA_Read(void)//GPIO_PinState是一个枚举类型
{
	delay_us(1);
	return HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_13);
}



static inline void I2C_Start(void)
{
	SDA_Set(1);//兼容重复起始条件
	SCL_Set(1);
	
	SDA_Set(0);
	SCL_Set(0);
}

static inline void I2C_Stop(void)
{
	SDA_Set(0);//终止条件前我们不确定SDA的状态，先把他拉低。每个环节开始之前都会保证SCL是低电平
	
	SCL_Set(1);
	SDA_Set(1);
}


static inline void I2C_SendAsk(uint8_t Bit)
{
	if(Bit)
		SDA_Set(!Bit);//不加大括号其实就是因为if判断只需要执行后面一个语句，意思是如果Bit>0,我们就执行发送应答，也是为了后续的发送多个字节方便判断
	SCL_Set(1);
	SCL_Set(0);
}

static inline uint8_t I2C_ReceiveAsk(void)
{
	GPIO_PinState AckBit;
	SDA_Set(1);
	SCL_Set(1);
	AckBit=(GPIO_PinState)!SDA_Read();//给应答的值取反，因为AckBit=1的时候没有应答，取反好辩认
	I2C_INFO("%s", AckBit ? "ACK" : "NACK");//如果AckBit为真（非0）AckBit=“ACK”即接收到了应答
	//I2C_INFO是我们宏定义的函数，本质就是打印函数
	SCL_Set(0);
	
	return AckBit;
}

static uint8_t I2C_SendByte(uint8_t Byte)//排查了一天，就是这个函数的问题，延时不够，BQ芯片跟不上发送的频率
{
	uint8_t i;
	for(i=0;i<8;i++)
	{
		SDA_Set(Byte&(0x80>>i));
		delay_us(2);//给SDA稳定时间，不然可能SCL拉高的时候SDA还没有稳定读出错误数据
		
		SCL_Set(1);
		delay_us(5);//给SCL稳定时间，因为从机并不是 “瞬间读取” SDA，而是需要稳定的高电平窗口来完成采样
		
		SCL_Set(0);
	}
	return I2C_ReceiveAsk();//枚举类型和整数类型是兼容的，可以直接返回uint8_t
	//这里发送数据我们弄了个返回值主要是为了后续发送多个字节数据做准备，如果返回值为0说明没有收到应答
	//其实就相当于自动在主机发送数据之后加入了接受应答操作
}

static uint8_t I2C_ReadByte(void)//为什么读操作的时候不会因为延时出问题呢
	{//因为读和发送不一样，我们拉高SCL进行读已经确保读到数据（if操作）才拉低SCL，然后从机发数据
		//发送数据的时候，我们拉高SCL然后得维持一段时间给从机读数据，相当于给从机一个if操作时间
		//但是延时短还是不妥，因为拉低SCL到拉高SCL给的缓冲时间太短，从机可能反应不过来
		//就像我们读操作的时候，在SCL低电平期间改变数据，数据还没来得及稳定就拉高了导致读错
		//这里没出错说明BQ芯片给力，但是还是得加入一定的延时，这里懒得加了，反而加快了执行速度
	uint8_t temp=0x00;
	uint8_t i;
	SDA_Set(1);//放手给从机操作权
	for(i=0;i<8;i++)
	{
		SCL_Set(1);//拉高SCL进行读数据
		if(SDA_Read()==GPIO_PIN_SET)
		{
			temp|=(0x80>>i);
		}
		SCL_Set(0);//拉低SCL让从机进行写数据
	}
	return temp;
}

static uint16_t I2C_SendBytes(struct I2C_MessageTypeDef *msg)
{
	uint8_t ret;
	const uint8_t *ptr = msg->buf;
	uint16_t bytes = 0, count = msg->tLen;
	
	while (count > 0)
	{
		// 发送控制字节，虽然在if判断里，但也是执行了写操作
		//这个判断逻辑是先判断msg的flag状态是否是控制状态，比如flag是0x01,
		//I2C_CONTROL_BYTE定义为了0x40，位与操作之后就是0x00。此时不会执行后续的判断，也就跳过了写控制操作
		//若前面的为真后续再与I2C_SendByte进行逻辑与操作，此时就已经执行了控制发送操作
		if (msg->flags & I2C_CONTROL_BYTE && I2C_SendByte(msg->cByte) == 0) 
		{
			I2C_WARNING("send bytes: NACK.");
			break;
		}

		ret = msg->flags & I2C_SAME_BYTE ? I2C_SendByte( msg->sByte) : I2C_SendByte( *ptr) , ptr++;
		//如果msg->flags & I2C_SAME_BYTE为真，执行发送重复字节。反之执行正常发送数据，并且指针自增
		if ((ret > 0) || (msg->flags & I2C_IGNORE_NACK && (ret == 0)))
			
		{ //ret>0是判断是否有应答，后面的是忽略应答状态，加一个ret==0的判断加了个冗余，
			//如果ret因为干扰或其他因素变成了负数，没有ret==0，I2C_IGNORE_NACK状态就会执行程序导致错误
			//按道理讲ret=-2这种极端情况发生，应该还需要一个状态处理，这里没有写，我自己加了一个else判断
			count --;			
			bytes ++;
		}
		else if (ret == 0)
		{
			I2C_WARNING("send bytes: NACK.");
			break;
		}
		else//处理极端ret值的情况
		{
			I2C_ERROR("Unexpected ret value: %d (invalid)", ret);
			break;//break是跳出这个while循环，如果用return那就是直接结束整个函数
		}
	}

	return bytes;
}

static uint16_t I2C_ReadBytes(struct I2C_MessageTypeDef *msg)
{
	uint8_t val;//创建变量用于存储读取的值
	uint8_t *p=msg->buf;//创建指针指向我们需要存储的数组
	uint16_t bytes=0,count=msg->tLen;//count就是我们需要应答的数量
	
	while(count>0)
	{
		val=I2C_ReadByte();
		*p=val;//把val的值存到数组里
		bytes++;
		
		p++;
		count--;
		
		I2C_INFO("recieve bytes: 0x%02x, %s",
					val, (msg->flags & I2C_NO_READ_ACK) ?
					"(No ACK/NACK)" : (count ? "ACK" : "NACK"));
		//打印一下信息
		
		if(!(msg->flags & I2C_NO_READ_ACK))//I2C_NO_READ_ACK状态是读取后不发送应答，不是这个状态我们就发送应答
		{
			I2C_SendAsk(count);//发送应答，只要count还大于0我们就接着发需要的应答
		}
	}
	return bytes;//返回成功接受的字节数量
}

static uint8_t I2C_SendAddress(uint8_t addr, uint32_t retries)//这个函数用来检查发送设备地址之后是否有应答
{
	uint8_t i, ret = 0;

	for (i = 0; i <= retries; i++)
	{
		ret = I2C_SendByte(addr);
		if (ret == 1)
		{
			I2C_INFO("response ok.");
			break;
		}
		else if (i == retries)
		{
			I2C_WARNING("no response, please check slave device.");
			break;
		}
		I2C_WARNING("no response, attempt to resend the address. number:%d.", i);
		I2C_Stop();
		I2C_Start();
	}

	return ret;
}

static uint8_t I2C_BitSendAddress(struct I2C_BusTypeDef *bus,struct I2C_MessageTypeDef *msg)
{
	uint8_t flags=msg->flags;
	uint8_t addr=msg->addr<<1;//由于是七位地址，我们要左移一位
	uint8_t ret,retries=bus->retries;//retrie是我们定义的响应次数
	
	if(flags & I2C_IGNORE_NACK)//如果是忽略应答模式，我们不需要响应
		retries=0;
		
	if(flags & I2C_RD )//如果是指定地址读，我们把七位地址最后一位置1，反之上面我们定义了最后一位是0
	{
		addr |=1;//指定地址读模式
	}
	ret=I2C_SendAddress(addr,retries);
	if((ret!=1)&&(retries!=0))//如果没有应答并且不是忽略应答模式
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

//完整的一个发送信息流程
uint32_t I2C_TransferMessages(struct I2C_BusTypeDef *bus, struct I2C_MessageTypeDef msgs[], uint32_t num)
{
	struct I2C_MessageTypeDef *msg;
	uint32_t i, ret = 0;
	uint8_t ignore_nack;

	if (NULL == bus || NULL == msgs || num == 0)return ret;// 参数合法性检查

	if (bus->lock) bus->lock();// 获取总线锁（多线程独占访问）

	for (i = 0; i < num; i++)
	{
		msg = &msgs[i];//兼容处理多条信息的操作。比如我们进行指定地址写，我们只需要一条信息，msg里面的
		//buf信息我们可以直接把控制以及所发送的数据直接写到数组。但如果是指定地址读，我们需要两条信息
		//一条是写操作把寄存器地址写进去，然后进行读操作，二者的flag状态不一样
		ignore_nack = msg->flags & I2C_IGNORE_NACK;
		if (!(msg->flags & I2C_NO_START))//如果我们不发送起始信号（I2C_NO_START状态），就直接跳过
		{
			if (i)
			{
				I2C_Start();//我们的start兼容了重复起始
			}
			else
			{
				I2C_INFO("send start condition");
				I2C_Start();
			}
			ret = I2C_BitSendAddress(bus, msg);
			if ((ret != 1) && !ignore_nack)
			{
				I2C_WARNING("receive NACK from device addr 0x%02x msg %d", msgs[i].addr, i);
				goto out;//如果应答失效，跳转到out执行程序
			}
		}
		if (msg->flags & I2C_RD)//I2C读数据模式
		{
			ret = I2C_ReadBytes(msg);
			msg->rLen = ret;
			I2C_INFO("read %d byte%s", ret, ret == 1 ? "" : "s");
		}
		else
		{
			ret = I2C_SendBytes(msg);	//I2C写数据模式
			msg->rLen = ret;//实际接收的数据长度
			I2C_INFO("write %d byte%s", ret, ret == 1 ? "" : "s");				
			if (msg->rLen != msg->tLen)//若实际长度与计划长度（msg->tLen）不符，判定发送失败，设置ret=0并跳转out
			{
				ret = 0;
				goto out;
			}
			
		}
	}
	ret = i;

out:
	if (!(msg->flags & I2C_NO_STOP))//如果不是传输完不发送结束信号模式则我们可以停止I2C
	{
		I2C_INFO("send stop condition");
		I2C_Stop();
	}

	if(bus->unlock) bus->unlock();
	
	return ret;
}

int I2C_BusInitialize(void)
{
	I2C_BusHardwareInitialize(&i2c1);//初始化一下I2C所需硬件，其实我们在CUBEMX已经设定好了GPIO口，感觉这步没有必要
	
	return 0;
}
