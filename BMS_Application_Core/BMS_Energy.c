#include "main.h"
#include "BMS_Energy.h"
#include <rtthread.h>
#include "BMS_Monitor.h"
#include "BMS_Anasys.h"
#include "BMS_Protect.h"
#include "BMS_Protect_Interface.h"//保护函数的中间层，里面写了充放电控制函数，这里得用上
#define DBG_TAG "Energy"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

static void BMS_EnergyTaskEntry(void *paramter);
static void BMS_BalanceTimerEntry(void *paramter);

static void BMS_EnergyChgDsgManage(void);
static void BMS_EnergyBalanceManage(void);

static BMS_StateTypedef BMS_CHGStateBackup;//用于储存上一次的充放电状态，防止重复充电操作增加运行内存
static BMS_StateTypedef BMS_DSGStateBackup;


BMS_EnergyDataTypedef EnergyData=
{
	.SocStopChg=0.98,//停止充电的阈值
	.SocStartChg=0.9,//待机状态开启充电的阈值
	.SocStopDsg=0.02,//停止放电阈值
	.SocStartDsg=0.1, //待机状态开启放电的阈值
	
	
	.BalanceStartVoltage = 3.30,// 均衡起始电压(V)
	.BalanceDiffeVoltage = 0.05,// 均衡差异电压(V)
	.BalanceCycleTime 	 = 30,  // 均衡周期时间(s)
	.BalanceRecord 		 	 = BMS_CELL_NULL,
};

static rt_timer_t pTimerBalance;
static bool BalanceStartFlag = false;
static uint32_t BalanceVoltRiseTime;

void BMS_EnergyInit(void)
{
	rt_thread_t thread;
	
	thread=rt_thread_create("Energy",BMS_EnergyTaskEntry,NULL,512,22,25);
	
	if (thread == NULL)
	{
		LOG_E("Create Task Fail");
	}

	rt_thread_startup(thread);
	
	
	pTimerBalance = rt_timer_create("Balance", 
									BMS_BalanceTimerEntry,
									NULL,
									20,
									RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);

	if (pTimerBalance == NULL)
	{
		LOG_E("Create Timer Fail");
	}
}

static void BMS_EnergyTaskEntry(void *paramter)
{
	BMS_CHGStateBackup=Globle_State.Charge;
	BMS_DSGStateBackup=Globle_State.Discharge;
	rt_thread_mdelay(500);
	while(1)
	{
		BMS_EnergyBalanceManage();//均衡管理
		BMS_EnergyChgDsgManage();//充放电管理
		rt_thread_mdelay(ENERGY_TASK_PERIOD);
	}
}

static void BMS_EnergyChgDsgManage(void)
{
	switch(Globle_State.Sysmod)
	{
		case BMS_MODE_CHARGE:
		{
			if(BMS_AnalysisData.SOC>EnergyData.SocStopChg)
			{
				BMS_CtrlCharge_Interface(BQ_STATE_DISABLE);//超过阈值关闭充电
				LOG_I("Stop Charge");
			}
		}break;
		
		case BMS_MODE_DISCHARGE:
		{
			if(BMS_AnalysisData.SOC<EnergyData.SocStopDsg)
			{
				BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);//超过阈值关闭放电
				LOG_I("Stop Discharge");
			}
		}break;
		
		case BMS_MODE_STANDBY:
		{
			if(Globle_State.Charge==BMS_STATE_ENABLE)//这个充电状态后续会在shell命令里面管理
			{
				if(BMS_Protect.alert & FLAG_ALERT_CHG_MASK)//如果此时没有发生充电报警
				{
					if(EnergyData.BalanceReleaseFlag==false)//如果均衡已经释放了
					{
						if(BMS_AnalysisData.SOC<EnergyData.SocStartChg)//SOC小于阈值，此时可以进入充电
						{
							BMS_CtrlCharge_Interface(BQ_STATE_ENABLE);
							LOG_I("Start Charge");
						}
					}
				}
			}
			
			else if(Globle_State.Discharge==BMS_STATE_ENABLE)
			{
				if(BMS_Protect.alert & FLAG_ALERT_DSG_MASK)
				{
					if(BMS_AnalysisData.SOC>EnergyData.SocStartDsg)
					{
						BMS_CtrlDischarge_Interface(BQ_STATE_ENABLE);
						LOG_I("Start Discharge");
					}
				}
			}
		}break;
		default:break;
	}
	
		// 可通过命令快速关闭充放电
	if(BMS_CHGStateBackup!=Globle_State.Charge)
	{
		if(Globle_State.Charge==BMS_STATE_DISABLE)//只允许快速关闭，以及睡眠模式的快速开启
		{
			BMS_CtrlCharge_Interface(BQ_STATE_DISABLE);
		}
		else if((Globle_State.Sysmod==BMS_MODE_SLEEP) && (Globle_State.Charge==BMS_STATE_ENABLE))
		{
			BMS_CtrlCharge_Interface(BQ_STATE_ENABLE);
			LOG_I("Start Charge");
		}
		BMS_CHGStateBackup=Globle_State.Charge;
	}
	
	else if(BMS_DSGStateBackup!=Globle_State.Discharge)
	{
		if(Globle_State.Discharge==BMS_STATE_DISABLE)//只允许快速关闭，以及睡眠模式的快速开启
		{
			BMS_CtrlDischarge_Interface(BQ_STATE_DISABLE);
		}
		else if((Globle_State.Sysmod==BMS_MODE_SLEEP) && (Globle_State.Discharge==BMS_STATE_ENABLE))
		{
			BMS_CtrlDischarge_Interface(BQ_STATE_ENABLE);
		}
		BMS_DSGStateBackup=Globle_State.Discharge;
	}
	
}

// 用于均衡计数的定时器回调入口
static void BMS_BalanceTimerEntry(void *paramter)
{
	BQ769X0_CellBalanceControl((BQ769X0_CellIndexTypedef)BMS_CELL_ALL,(BQ769X0_StateTypedef)BMS_STATE_DISABLE);

	EnergyData.BalanceRecord = BMS_CELL_NULL;
	
	BalanceStartFlag = false;

	// 用于均衡电压回升计时
	BalanceVoltRiseTime = rt_tick_from_millisecond(5000) + rt_tick_get();
	//rt_tick_from_millisecond(5000)：将 “毫秒级延迟” 转换成系统tick数（RT-Thread 中，1 个tick默认等于 1 毫秒，比如BALANCE_VOLT_RISE_DELAY=200，则转换后为 200 个tick）。
	//rt_tick_get()的核心是返回系统启动后的累计tick数
	//也就是说定时结束，rt_tick_get()依旧在增长，这是系统运行的总时间。我们硬性让BalanceVoltRiseTime加上5s
	//意思就是在均衡结束之后，系统运作5s之后才可以进入下一次均衡，和我们之前程序直接进行运行周期累加不一样
	
	LOG_I("Balance Timer End");
}

// 启动均衡定时器计数任务
static void BMS_BalanceStartTimer(uint32_t sec)
{
	uint32_t tick;

	tick = rt_tick_from_millisecond(sec * 1000);
	rt_timer_control(pTimerBalance, RT_TIMER_CTRL_SET_TIME, &tick);
	rt_timer_start(pTimerBalance);

	LOG_I("Balance Timer Start");
}

static bool BMS_EnergyBalanceCheck(void)
{
		// 上一轮均衡时间等待还未结束
	if (BalanceVoltRiseTime >= rt_tick_get())//定时器到期执行的回调函数会重制BalanceVoltRiseTime，也就是
															//当前时间加上我们设定的5s，
	{
		return false;
	}

	// 均衡定时器启动
	if (BalanceStartFlag != false)
	{
		return false;
	}
	
		// 未处于待机和充电模式
	if (Globle_State.Sysmod != BMS_MODE_STANDBY && Globle_State.Sysmod != BMS_MODE_CHARGE)		
	{
		EnergyData.BalanceReleaseFlag = false;
		
		return false;				
	}
	
	// 最高电池电压小于均衡起始电压
	if (MonitorData.BMS_CellData[4].CellVoltage < EnergyData.BalanceStartVoltage)
	{
		EnergyData.BalanceReleaseFlag = false;
		
		return false;				
	}
	
		// 最高和最低电池的电压差未达到均衡条件
	if (BMS_AnalysisData.MaxVoltageDifference < EnergyData.BalanceDiffeVoltage)
	{	
		EnergyData.BalanceReleaseFlag = false;
		
		return false;
	}
	
	EnergyData.BalanceReleaseFlag = true;

	return true;
}

// 均衡电池筛选
static void BMS_EnergyBalanceFilter(void)
{
	float CmpVoltage;
	float MinVoltage = MonitorData.BMS_CellData[0].CellVoltage;


	/*
	// 相邻单元能同时均衡的情况,BQ不能相邻同时均衡,未测试过
	for(index = 1; index < BMS_GlobalParam.Cell_Real_Number + 1; index++)
	{
		CmpVoltage = BMS_MonitorData.CellData[BMS_GlobalParam.Cell_Real_Number-index].CellVoltage;
	
		// 是否达到均衡压差条件
		if (CmpVoltage - MinVoltage > BMS_EnergyData.BalanceDiffeVoltage)
		{
			BMS_EnergyData.BalanceRecord |= 1 << BMS_MonitorData.CellData[BMS_GlobalParam.Cell_Real_Number-index].CellNumber;
		}
		else
		{
			break;
		}
	}
	*/
	
	
	
	
	/* 适用于相邻单元不能同时均衡且均衡顺序不按照从大到小进行
	for(index = 0; index < BMS_GlobalParam.Cell_Real_Number; index++)
	{
		if (BMS_MonitorData.CellVoltage[index] - MinVoltage > BMS_EnergyData.BalanceDiffeVoltage)
		{
			BMS_INFO("Balance Cell:%d", index + 1);
			BMS_EnergyData.BalanceRecord |= 1 << index++;
		}
	}
	*/
	
	
	
	
	/* 适用于相邻单元不能同时均衡且均衡顺序按照从大到小进行 */	
	for(uint8_t index = 1; index < 6; index++)
	{
		CmpVoltage = MonitorData.BMS_CellData[5-index].CellVoltage;
	
		if (CmpVoltage - MinVoltage > EnergyData.BalanceDiffeVoltage)
		{
			bool result = false;
			uint8_t CellNumber = MonitorData.BMS_CellData[5-index].CellNumber;
	
			if (CellNumber == 0)  
			{
				// 第一节电芯满足均衡压差情况,判断第二节是否添加了均衡标志
				if ((EnergyData.BalanceRecord & 0x02) == 0)
				{								
					result = true;
				}
			}
			else if (CellNumber + 1 == 5)
			{
				// 最后一节电芯满足均衡压差情况,判断前一节是否添加了均衡标志
				if ((EnergyData.BalanceRecord & (1 << (CellNumber - 1))) == 0)
				{
					result = true;
				}
			}
			else
			{
				// 其他电芯满足均衡压差情况
				if (((EnergyData.BalanceRecord & (1 << (CellNumber - 1))) == 0) &&
				   ((EnergyData.BalanceRecord & (1 << (CellNumber + 1))) == 0))
				{
					result = true;
				}
			}
			
			if (result == true)
			{
				LOG_I("Balance Cell:%d", CellNumber + 1);
				EnergyData.BalanceRecord |= 1 << CellNumber;
			}
		}
		else 
		{
			break;
		}
	}
	
}

// 均衡启动
static void BMS_EnergyBalanceStart(void)
{
	if (EnergyData.BalanceRecord != BMS_CELL_NULL)
	{
		// 操作实际硬件
		BQ769X0_CellBalanceControl((BQ769X0_CellIndexTypedef)EnergyData.BalanceRecord,(BQ769X0_StateTypedef)BMS_STATE_ENABLE);
		BMS_BalanceStartTimer(EnergyData.BalanceCycleTime);
	
		BalanceStartFlag = true;
		
		LOG_I("Balance Start");
	}
}



// 均衡管理
static void BMS_EnergyBalanceManage(void)
{
	if (BMS_EnergyBalanceCheck() == true)
	{
		BMS_EnergyBalanceFilter();//筛选需要均衡的电芯
		BMS_EnergyBalanceStart();//开启均衡，同时开启定时器
	}
}