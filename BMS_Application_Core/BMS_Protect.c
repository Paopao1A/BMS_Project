#include "main.h"
#include <stdio.h>
#include <rtthread.h>
#include "BMS_Protect.h"
#include "BMS_Monitor.h"
#include "BMS_App.h"
#include "BMS_Protect_Interface.h"
#define DBG_TAG "Protect"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

void BMS_ProtectEntry(void* paramter);
static void BMS_ProtectTiggerMonitor(void);
static void BMS_ProtectRelieveMonitor(void);

BMS_ProtectTypedef BMS_Protect=//初始化一下保护的参数
{
	.alert=FlAG_ALERT_NO,
	.param=
	{
		.ShoutdownVoltage = TLB_SHUTDOWN_VOLTAGE,//在"BMS_App.h"定义了

		.OVProtect	= TLB_OV_PROTECT,// 充电过压保护电压(V)					由硬件完成(意思就是我们在硬件层定义好了)
		.OVRelieve	= TLB_OV_RELIEVE,// 充电过压恢复电压(V)
		.UVProtect	= TLB_UV_PROTECT,// 放电欠压保护电压(V)					由硬件完成
		.UVRelieve	= TLB_UV_RELIEVE,// 放电欠压恢复电压(V)
                                 
		.OCCProtect = INIT_OCC_MAX,  		// 充电过流阈值(A)
		.OCDProtect = INIT_OCD_MAX,  		// 放电过流阈值(A)			由硬件完成

		.OVDelay	= Init_OV_Delay,
		.UVDelay	= Init_UV_Delay,
		.OCDDlay	=	Init_OCD_Delay,
		.SCDDlay	=	Init_SCD_Delay,
		
		.OCDRelieve = INIT_OCD_RELIEVE,	// 放电过流恢复(S)
		.SCDRelieve = INIT_SCD_RELIEVE,	// 放电短路恢复(S)
		.OCCDelay	= INIT_OCC_DELAY,    // 充电过流延时(S)
		.OCCRelieve = INIT_OCC_RELIEVE,// 充电过流恢复(S)
		
		.OTCProtect = INIT_OTC_PROTECT,
		.OTCRelieve = INIT_OTC_RELIEVE,
		.OTDProtect = INIT_OTD_PROTECT,
		.OTDRelieve = INIT_OTD_RELIEVE,

		.LTCProtect = INIT_LTC_PROTECT,
		.LTCRelieve = INIT_LTC_RELIEVE,
		.LTDProtect = INIT_LTD_PROTECT,
		.LTDRelieve = INIT_LTD_RELIEVE,
	}                                	
};

void BMS_ProtectInit(void)
{
	rt_thread_t thread;
	thread=rt_thread_create("Protect",BMS_ProtectEntry,NULL,256,20,25);
	
	if(thread==NULL)
	{
		LOG_E("Create Task Fail");
	}
	rt_thread_startup(thread);
}

static void BMS_ProtectEntry(void* paramter)
{
	while(1)
	{
		BMS_ProtectTiggerMonitor();//保护触发监控
		BMS_ProtectRelieveMonitor();//保护释放监控
		rt_thread_mdelay(PROTECT_TASK_PERIOD);
	}
}

static void BMS_ChargeMonitor(void)
{
	static uint32_t ProtectCount = 0;//用于计时
	//软件触发部分
	if(MonitorData.BMS_BatCurrent>=BMS_Protect.param.OCCProtect)
	{
		ProtectCount+=PROTECT_TASK_PERIOD;//加上一个周期
		if(ProtectCount/1000>=BMS_Protect.param.OCCDelay)//充电过流我们定义的是s单位，所以ProtectCount得除1000
		{
			BMS_CtrlCharge_Interface(BQ_STATE_DISABLE);//关闭充电
			BMS_Protect.alert|=FlAG_ALERT_OCC;//用或逻辑记录多种状态，可能又过流又过压
			LOG_W("Charge:OCC Protect Tigger");
		}
	}
	else if (MonitorData.BMS_CellTemp> BMS_Protect.param.OTCProtect)
	{
		// 过温
		BMS_CtrlCharge_Interface(BQ_STATE_DISABLE);
		BMS_Protect.alert = FlAG_ALERT_OTC;	
		
		LOG_W("Charge:OTC Protect Tigger");
	}
	else if (MonitorData.BMS_CellTemp < BMS_Protect.param.LTCProtect)
	{
		// 低温
		BMS_CtrlCharge_Interface(BQ_STATE_DISABLE);
		BMS_Protect.alert = FlAG_ALERT_LTC;	

		LOG_W("Charge:LTC Protect Tigger");
	}
	else
	{
		// 复位计数
		ProtectCount = 0;
	}
}

static void BMS_DischargeMonitor(void)
{
	//放电过流由硬件完成了
	if (MonitorData.BMS_CellTemp> BMS_Protect.param.OTDProtect)
	{
		// 过温
		BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);
		BMS_Protect.alert = FlAG_ALERT_OTD;

		LOG_W("Discharge:OTD Protect Tigger");
	}
	else if (MonitorData.BMS_CellTemp< BMS_Protect.param.LTDProtect)
	{
		// 低温
		BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);		
		BMS_Protect.alert = FlAG_ALERT_LTD;	
		
		LOG_W("Discharge:LTD Protect Tigger");
	}
}


static void BMS_ProtectTiggerMonitor(void)
{
	switch(Globle_State.Sysmod)
	{
		case BMS_MODE_CHARGE:
		{
			BMS_ChargeMonitor();//充电保护管理
		}break;

		case BMS_MODE_DISCHARGE:
		{
			BMS_DischargeMonitor();//放电保护管理
		}break;

		case BMS_MODE_STANDBY:
		{
			//BMS_StandbyMonitor();
		}break;

		case BMS_MODE_SLEEP:
		{
			// 睡眠暂时没什么可监控的
		}break;
		
		case BMS_MODE_NULL:
			break;
	}
}

static void BMS_ProtectRelieveMonitor(void)//状态解除函数
{
	static uint32_t RelieveCountCHG=0,RelieveCountDSG = 0;
	if(BMS_Protect.alert != FlAG_ALERT_NO)//不是非报警模式
	{
		if(BMS_Protect.alert & FlAG_ALERT_OV)//如果此时是充电过压状态，用与是因为可能有多种状态一起报警
		{
			if(MonitorData.BMS_CellData[4].CellVoltage<BMS_Protect.param.OVRelieve)//如果电芯的最大电压小于释放电压
			{
				BMS_Protect.alert &=~FlAG_ALERT_OV;//清除相应的标志位
				LOG_I("Charge:OV Relieve");//充电过压硬件会完成等待释放延时然后重新开关等操作，不用我们自行编程
			}
		}
		
		else if(BMS_Protect.alert & FlAG_ALERT_OCC)//充电过流
		{
			//这里没有过流释放的阈值，就等待掉过流释放的时间就行了
			RelieveCountCHG += PROTECT_TASK_PERIOD;
			if(RelieveCountCHG/1000>=BMS_Protect.param.OCCRelieve)
			{
				RelieveCountCHG=0;//清0计数
				
				BMS_Protect.alert &= ~FlAG_ALERT_OCC;
				BMS_CtrlCharge_Interface(BQ_STATE_ENABLE);//重新开始充电
				LOG_I("Charge:OCC Relieve");
			}
		}
		
		else if (BMS_Protect.alert & FlAG_ALERT_OTC)
		{
			if (MonitorData.BMS_CellTemp < BMS_Protect.param.OTCRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_OTC;
				BMS_CtrlCharge_Interface(BQ_STATE_ENABLE);
				
				LOG_I("Charge:OTC Relieve");
			}
		}
		
		else if (BMS_Protect.alert & FlAG_ALERT_LTC)
		{
			if (MonitorData.BMS_CellTemp> BMS_Protect.param.LTCRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_LTC;
				BMS_CtrlCharge_Interface(BQ_STATE_ENABLE);
				
				LOG_I("Charge:LTC Relieve");
			}
		}
		if (BMS_Protect.alert & FlAG_ALERT_UV)
		{
			if (MonitorData.BMS_CellData[0].CellVoltage > BMS_Protect.param.UVRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_UV;
				
				LOG_I("Discharge:UV Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_OTD)
		{
			if (MonitorData.BMS_CellTemp< BMS_Protect.param.OTDRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_OTD;
				BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);
				
				LOG_I("Discharge:OTD Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_LTD)
		{
			if (MonitorData.BMS_CellTemp > BMS_Protect.param.LTDRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_LTD;
				BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);
				
				LOG_I("Discharge:LTD Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_OCD)
		{
			RelieveCountDSG += PROTECT_TASK_PERIOD;
			if (RelieveCountDSG / 1000 >= BMS_Protect.param.OCDRelieve)
			{
				RelieveCountDSG = 0;

				BMS_Protect.alert &= ~FlAG_ALERT_OCD;
				BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);

				LOG_I("Discharge:OCD Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_SCD)
		{
			RelieveCountDSG += PROTECT_TASK_PERIOD;
			if (RelieveCountDSG / 1000 >= BMS_Protect.param.SCDRelieve)
			{
				RelieveCountDSG = 0;

				BMS_Protect.alert &= ~FlAG_ALERT_SCD;		
				BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);

				LOG_I("Discharge:SCD Relieve");
			}
		}
	}
}

//硬件触发的函数，最后写入到我们最开始InitData的Alert结构体里面的函数指针里面
void BMS_ProtectHwOCD(void)
{
	if((BMS_Protect.alert & FlAG_ALERT_OCD)==0)//如果此时已经是放电过流报警，我们就跳过，防止多次触发
					//由于我们之前写的报警中断函数每次都会把相应的位重新置0，如果一直过流会一直触发报警信号
	{
		BMS_Protect.alert |=FlAG_ALERT_OCD;//置标志位
		BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);//关闭放电开关
		LOG_W("Discharge:OCD Protect Tigger");
	}
}

// 放电短路(SCD)硬件触发
void BMS_ProtectHwSCD(void)
{
	if ((BMS_Protect.alert & FlAG_ALERT_SCD) == FlAG_ALERT_NO) // 判断是为了防止多次触发
	{
		BMS_Protect.alert |= FlAG_ALERT_SCD;
		BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);
		LOG_W("Discharge:SCD Protect Tigger");
	}
}

// 充电过压(OV)硬件触发
void BMS_ProtectHwOV(void)
{
	if ((BMS_Protect.alert & FlAG_ALERT_OV) == FlAG_ALERT_NO) // 判断是为了防止多次触发
	{
		BMS_Protect.alert |= FlAG_ALERT_OV;
		BMS_CtrlCharge_Interface(BQ_STATE_DISABLE);
		LOG_W("Charge:OV Protect Tigger");
	}
}

// 放欠过压(UV)硬件触发
void BMS_ProtectHwUV(void)
{
	if ((BMS_Protect.alert & FlAG_ALERT_UV) == FlAG_ALERT_NO) // 判断是为了防止多次触发
	{
		BMS_Protect.alert |= FlAG_ALERT_UV;
		BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);
		LOG_W("Discharge:UV Protect Tigger");
	}
}


void BMS_ProtectHwDevice(void)
{
	LOG_W("BMS_ProtectHwDevice");
}

void BMS_ProtectHwOvrd(void)
{
	LOG_W("BMS_ProtectHwOvrd");
}
