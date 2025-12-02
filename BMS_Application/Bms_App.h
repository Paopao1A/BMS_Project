#ifndef __BMS_APP_H__
#define __BMS_APP_H__

/***************************** 电池保护相关参数 ***********************************/
// 三元锂电池(Ternary lithium battery)默认参数
#define TLB_OV_PROTECT			4.20	// 单体过压保护电压
#define TLB_OV_RELIEVE			4.18	// 单体过压恢复电压
#define TLB_UV_PROTECT			3.10	// 单体欠压保护电压
#define TLB_UV_RELIEVE			3.15	// 单体欠压恢复电压
#define TLB_SHUTDOWN_VOLTAGE	3.08	// 自动关机电压
#define TLB_BALANCE_VOLTAGE		3.30	// 均衡起始电压

#define Init_SCD_Delay	BQ_SCD_DELAY_100us // 放电短路延时时间(us)
#define Init_OCD_Delay	BQ_OCD_DELAY_320ms// 放电过流延时时间(mS)
#define Init_OV_Delay 	BQ_OV_DELAY_2s		// 充电过压保护延时时间
#define Init_UV_Delay 	BQ_UV_DELAY_4s		// 放电欠压保护延时时间
#define Init_OVPThreshold TLB_OV_PROTECT	//过压阈值
#define Init_UVPThreshold TLB_UV_PROTECT	//欠压阈值

#define	INIT_OCC_MAX				2.2		// 最大充电电流(A)
#define	INIT_OCD_MAX				2.2		// 最大放电电流(A),由BQ芯片控制,此参数改动不起作用,应该在drv_softi2c_bq769x0.c修改放电过流

#define INIT_OCD_RELIEVE	60					// 放电过流解除时间(S)
#define INIT_SCD_RELIEVE	60					// 放电短路解除时间(S)

#define INIT_OCC_DELAY		1		// 充电过流延时时间(S) OCC:Over Current Charge
#define INIT_OCC_RELIEVE	60		// 充电过流解除时间(S)

#define INIT_OTC_PROTECT	70		// 充电过温保护(℃) OTC:Over Temperature Charge
#define INIT_OTC_RELIEVE	60		// 充电过温解除(℃)

#define INIT_OTD_PROTECT	70		// 放电过温保护(℃) OTD:Over Temperature Discharge
#define INIT_OTD_RELIEVE	60		// 放电过温解除(℃)

#define INIT_LTC_PROTECT	-20		// 充电低温保护(℃) LTC:Low Temperature Charge
#define INIT_LTC_RELIEVE	-10		// 充电低温解除(℃)

#define INIT_LTD_PROTECT	-20		// 放电低温保护(℃) LTD:Low Temperature Discharge
#define INIT_LTD_RELIEVE	-10		// 放电低温解除(℃)	

void BMS_SysInit(void);

#endif
