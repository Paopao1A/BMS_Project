#include "main.h"
#include <stdio.h>
#include <rtthread.h>
#include "BMS_Monitor.h"
#include "BMS_Monitor_Interface.h"
#define DBG_TAG "Monitor"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"
#include <stdbool.h>

static bool FlagSampleIntCur = false;//是否可以进行电流采集了


static uint16_t CountCellVoltage = 0;//电压计时时间
static uint16_t CountTotolVoltage = 0;//总电压计时
static uint16_t CountCellTemp = 0;//温度计时
static uint16_t CountBatCurrent=0;//电流计时

static void BMS_MonitorEntry(void* paramter);
static void BMS_Monitor_Data(void);
static void BMS_Monitor_State(void);

BMS_MonitorDataTypedef MonitorData;//数据管理的全局变量

BMS_MonitorStateTypedef Globle_State= //定义全局状态
{
	.Sysmod=BMS_MODE_STANDBY,
	.Charge=BMS_STATE_DISABLE,
	.Discharge=BMS_STATE_DISABLE,
	.Balance=BMS_STATE_DISABLE,
};

void BMS_MonitorInit(void)
{
	rt_thread_t thread;
	
	thread = rt_thread_create("Monitor",BMS_MonitorEntry,NULL,512,19,25);//一开始遇到了不能创建线程的情况
	//检查了CUBEMX发现了没有使能动态创建线程的功能，回头使能一下就行。
	//分别是线程名，入口函数，入口函数的参数，分配的线程栈大小，优先级和分配的时间片。25就是25个时钟滴答，也就是25ms
	if (thread == NULL)
   {
	   LOG_E("Create Task Fail");
   }

	rt_thread_startup(thread);
}


static void BMS_MonitorEntry(void* paramter)
{
	while(1)
	{
	BMS_Monitor_Data();//数据监控函数
	BMS_Monitor_State();//状态监控函数
	rt_thread_mdelay(MONITOR_TASK_PERIOD);//线程休眠指定周期（定义每250ms）释放 CPU 资源给其他线程。
		//延时时间常理来说得大于执行函数较多时间，比如监控函数执行10ms，延时100ms。为什么要这样呢？
		//因为加入监控函数执行100ms，延时50ms，这就导致运行周期长，低优先级的线程总是分配不到CPU
	}
}

static void BMS_Monitor_Data(void)
{
	//电压监控
	
	//先进行电压的采集
	CountCellVoltage+=MONITOR_TASK_PERIOD;//这个计时是用于降低电压的采样周期的，减少CPU的损耗
	if(CountCellVoltage>=MONITOR_TASK_PERIOD)//一个周期进行一次
	{
	BMS_MonitorVoltage_Itf();
	CountCellVoltage=0;
	}
	
	//总电压采集
	CountTotolVoltage+=MONITOR_TASK_PERIOD;
	if(CountTotolVoltage>=MONITOR_TASK_PERIOD)//一个周期进行一次
	{
	BMS_MonitorTotolVoltage_Itf();
	CountCellVoltage=0;
	}
	
	//电池温度采集
	CountCellTemp+=MONITOR_TASK_PERIOD;
	if(CountCellTemp>=8*MONITOR_TASK_PERIOD)//八个周期进行一次
	{
		BMS_MonitorCellTemp_Itf();
		CountCellTemp=0;
	}
	
	//电流采集,特殊一点，因为电流采集有相应的标志位辅助执行，也就是START_UP的CC位
	if(FlagSampleIntCur==true)
	{
		BMS_MonitorBatCurrent_Itf();
		FlagSampleIntCur=false;
	}
}

// 系统模式监控
// BatteryCurrent > 20mA || BatteryCurrent < -20mA  处于非睡眠模式
// BatteryCurrent < 20mA || BatteryCurrent > -20mA  处于待机模式或者睡眠模式
// BatteryCurrent <= -20mA 处于放电模式
// BatteryCurrent >=  20mA 处于充电模式
// 20mA这个值根据最终硬件实测决定，测量电池未充放情况下系统静态功耗最大，不然会误触发进入模式
static void BMS_Monitor_State(void)
{
	static BMS_MonitorModeTypedef Model_Now=BMS_MODE_NULL;//创建一个变量用于保存现在的模式
	float BMS_STANDBY_CURRENT_DCH=-0.02,BMS_STANDBY_CURRENT_CHG=0.02;//待机状态电流的上下限
	static uint16_t StandbyCount;//用于待机模式的监控，如果时间超过阈值就进入休眠状态
	
	if(Globle_State.Sysmod!=Model_Now)//如果状态改变了我们打印信息，不改变就不发送信息
	{
		switch(Globle_State.Sysmod)
		{
      case BMS_MODE_SLEEP:
        LOG_I("system entry sleep mode");
        break;
			case BMS_MODE_STANDBY:
				LOG_I("system entry standby mode");
				break;
			case BMS_MODE_CHARGE:
        LOG_I("system entry charge mode");
        break;
      case BMS_MODE_DISCHARGE:
        LOG_I("system entry discharge mode");
        break;
      case BMS_MODE_NULL:
        break;
		}
		Model_Now=Globle_State.Sysmod;
	}
	
	switch(Globle_State.Sysmod)//状态机思路
	{
		case BMS_MODE_SLEEP:
			if(MonitorData.BMS_BatCurrent>=BMS_STANDBY_CURRENT_CHG)
			{
				Globle_State.Sysmod=BMS_MODE_CHARGE;
			}
			else if(MonitorData.BMS_BatCurrent<=BMS_STANDBY_CURRENT_DCH)
			{
				Globle_State.Sysmod=BMS_MODE_DISCHARGE;
			}
			break;
			
		case BMS_MODE_STANDBY:
			if(MonitorData.BMS_BatCurrent>=BMS_STANDBY_CURRENT_CHG)
			{
				Globle_State.Sysmod=BMS_MODE_CHARGE;
				StandbyCount=0;
			}
			else if(MonitorData.BMS_BatCurrent<=BMS_STANDBY_CURRENT_DCH)
			{
				Globle_State.Sysmod=BMS_MODE_DISCHARGE;
				StandbyCount=0;
			}
			else//这个else不能去掉，因为如果发生了状态切换，后面的程序依旧执行，导致累加器又加了一次，下次用的就是不对的累加器
			{
				StandbyCount+=MONITOR_TASK_PERIOD;
				if(StandbyCount>=4*MONITOR_TASK_PERIOD)//如果四个周期都没有工况改变，我们进入睡眠模式
				{
					Globle_State.Sysmod=BMS_MODE_SLEEP;
					StandbyCount=0;
				}
			}
			break;
			
		case BMS_MODE_CHARGE:
			if(MonitorData.BMS_BatCurrent<=BMS_STANDBY_CURRENT_CHG)
			{
				if(MonitorData.BMS_BatCurrent<BMS_STANDBY_CURRENT_DCH)
				{
					Globle_State.Sysmod=BMS_MODE_DISCHARGE;
				}
				else if(MonitorData.BMS_BatCurrent>=BMS_STANDBY_CURRENT_DCH)
				{
					Globle_State.Sysmod=BMS_MODE_STANDBY;
				}
			}
			break;
			
		case BMS_MODE_DISCHARGE:
			if(MonitorData.BMS_BatCurrent>=BMS_STANDBY_CURRENT_DCH)
			{
				if(MonitorData.BMS_BatCurrent>BMS_STANDBY_CURRENT_CHG)
				{
					Globle_State.Sysmod=BMS_MODE_CHARGE;
				}
				else if(MonitorData.BMS_BatCurrent<=BMS_STANDBY_CURRENT_DCH)
				{
					Globle_State.Sysmod=BMS_MODE_STANDBY;
				}
			}
			break;
			
		case BMS_MODE_NULL:
			break;
	}
}

void BMS_MonitorHwCurrent(void)
{
	FlagSampleIntCur = true;
}