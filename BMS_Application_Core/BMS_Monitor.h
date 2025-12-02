#ifndef __BMS_MONITOR_H__
#define __BMS_MONITOR_H__

#define MONITOR_TASK_STACK_SIZE	512//线程大小
#define MONITOR_TASK_PRIORITY	19		//优先级
#define MONITOR_TASK_TIMESLICE	25	//时间片大小

#define MONITOR_TASK_PERIOD		250		//任务的周期

typedef struct
{
	float CellVoltage;
	uint8_t CellNumber;
}BMS_CellDataTypedef;

typedef struct
{
	float BMS_CellVoltage[5];//没有排序的电芯电压
	BMS_CellDataTypedef BMS_CellData[5];
	float BMS_CellTotolVoltage;
	float BMS_CellTemp;
	float BMS_BatCurrent;
}BMS_MonitorDataTypedef;//存放管理数据

typedef enum
{
	BMS_STATE_ENABLE,
	BMS_STATE_DISABLE
}BMS_StateTypedef;

typedef enum
{
	BMS_MODE_NULL = 0x00,
	BMS_MODE_CHARGE,	// 充电模式
	BMS_MODE_DISCHARGE,	// 放电模式
	BMS_MODE_STANDBY,	// 待机模式
	BMS_MODE_SLEEP,		// 睡眠模式
}BMS_MonitorModeTypedef;//不同的模式

typedef struct
{
	BMS_MonitorModeTypedef Sysmod;
	BMS_StateTypedef Charge;	// 充电状态
	BMS_StateTypedef Discharge;	// 放电状态
	BMS_StateTypedef Balance;	// 均衡状态
}BMS_MonitorStateTypedef;//定义一个模式以及状态，比如充电模式，处于充电以及均衡状态

extern BMS_MonitorDataTypedef MonitorData;
extern BMS_MonitorStateTypedef Globle_State;//全局状态

void BMS_MonitorInit(void);
void BMS_MonitorHwCurrent(void);
#endif
