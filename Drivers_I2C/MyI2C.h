#ifndef __MYI2C_H__
#define __MYI2C_H__

#define I2C_DEBUG_LEVEL 2

#if I2C_DEBUG_LEVEL == 0
#define I2C_INFO(fmt, arg...) 
#define I2C_WARNING(fmt, arg...)
#define I2C_ERROR(fmt, arg...)

#elif I2C_DEBUG_LEVEL == 1
#define I2C_INFO(fmt, arg...)   		rt_kprintf("<<-I2C-INFO->> "fmt"\r\n",##arg)
#define I2C_WARNING(fmt, arg...)
#define I2C_ERROR(fmt, arg...)

#elif I2C_DEBUG_LEVEL == 2
#define I2C_INFO(fmt, arg...)
#define I2C_WARNING(fmt, arg...)		rt_kprintf("<<-I2C-WARNING->> "fmt"\r\n",##arg)
#define I2C_ERROR(fmt, arg...)

#elif I2C_DEBUG_LEVEL == 3
#define I2C_INFO(fmt, arg...)
#define I2C_WARNING(fmt, arg...)
#define I2C_ERROR(fmt, arg...)	 		rt_kprintf("<<-I2C-ERROR->> "fmt"\r\n",##arg)
#endif

//这段代码通过 “调试级别 + 条件编译” 实现了 I2C 日志的分级管理，
//是嵌入式开发中平衡 “调试便利性” 和 “系统轻量化” 的经典设计。
//开发时可根据需求选择级别（如详细调试用 1 级，抓错用 3 级）
//比如说等级为1的时候我们只能调用I2C_INFO(fmt, arg...)，等级为3的时候只可以调用错误信息。
//开发的初期我们就可以用等级1，到后面做到安全保护这类方面可以慢慢增加等级

#define I2C_WR              0x00       		/* 写标志 */
#define I2C_RD              (1 << 0)      	/* 读标志 */
#define I2C_ADDR_10BIT      (1 << 1)      	/* 10 位地址模式 */
#define I2C_NO_START        (1 << 2)      	/* 无开始条件 */
#define I2C_IGNORE_NACK     (1 << 3)      	/* 忽视 NACK */
#define I2C_NO_READ_ACK     (1 << 4)      	/* 读的时候不发送 ACK */
#define I2C_NO_STOP         (1 << 5) 		/* 传输完不发送停止信号 */

// 下面两种标志位我自己添加的
#define I2C_CONTROL_BYTE    (1 << 6) 		/* 每发送一个数据之前需要发送一个控制字节用来表示之后的数据字节是命令还是数据(有的场景会用到比如：ssd1306驱动屏) */
#define I2C_SAME_BYTE       (1 << 7) 		/* 连续发送msg.tLen个msg.sByte数据字节,发送相同字节会用到避免循环调用传输函数 */


/* i2c消息定义 */
struct I2C_MessageTypeDef
{
	uint8_t   *buf;    		// 传输指定缓冲区的数据，一般都是一个数组，比如I2C_MessageTypeDef msg.buf=arr 
	uint16_t  addr;    		// 从设备地址
	uint16_t  tLen;		 	// 需要传输的数据长度(最大65535),加了I2C_CONTROL_BYTE标志不算上该长度，写模式需要这个
	uint16_t  rLen; 	 	// 返回成功传输的数据长度,如果加了I2C_CONTROL_BYTE标志并不会算上控制字节长度
	uint8_t   flags;	 	// 标志位,就是我们上面自己定义的状态
	uint8_t   cByte;	 	// I2C_CONTROL_BYTE，发送控制字节，比如发送存储从机的 “寄存器地址”
	uint8_t   sByte;	 	// I2C_SAME_BYTE，需要重复发送的字节
};
//由于I2C一般都是先发送从机设备地址，然后发送控制字节寻找寄存器，所以分开定义更加直观


struct I2C_BusTypeDef//定义I2C句柄，主要包含GPIO以及总线锁
{
	GPIO_TypeDef *gpiox;
	uint32_t gpio_rcc;
	uint16_t sda_gpio_pin;
	uint16_t scl_gpio_pin;
	
	uint32_t retries; // 发地址无响应重试次数(最大不能超过255)
	
	void (*udelay)(uint32_t us);//延迟函数
	void (*lockInit)(void);
	void (*lock)(void);
	void (*unlock)(void);
};

extern struct I2C_BusTypeDef i2c1;

int I2C_BusInitialize(void);
uint32_t I2C_TransferMessages(struct I2C_BusTypeDef *bus, struct I2C_MessageTypeDef msgs[], uint32_t num);
#endif
