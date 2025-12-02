#ifndef __BMS_ENERGY_H__
#define __BMS_ENERGY_H__

#include <stdbool.h>

#define ENERGY_TASK_PERIOD		200

typedef enum
{
	BMS_CELL_NULL		= 0x0000,
	BMS_CELL_INDEX1 	= 0x0001,
	BMS_CELL_INDEX2 	= 0x0002,
	BMS_CELL_INDEX3 	= 0x0004,
	BMS_CELL_INDEX4 	= 0x0008,
	BMS_CELL_INDEX5 	= 0x0010,
	BMS_CELL_INDEX6 	= 0x0020,
	BMS_CELL_INDEX7 	= 0x0040,
	BMS_CELL_INDEX8 	= 0x0080,
	BMS_CELL_INDEX9 	= 0x0100,
	BMS_CELL_INDEX10	= 0x0200,	
	BMS_CELL_INDEX11 	= 0x0400,
	BMS_CELL_INDEX12 	= 0x0800,
	BMS_CELL_INDEX13 	= 0x1000,
	BMS_CELL_INDEX14 	= 0x2000,
	BMS_CELL_INDEX15	= 0x4000,
	BMS_CELL_ALL		= 0x7FFF,
}BMS_CellIndexTypedef;

typedef struct
{
	float SocStopChg;			// 停止充电SOC值
	float SocStartChg;			// 启动充电SOC值
	float SocStopDsg;			// 停止放电SOC值
	float SocStartDsg;			// 启动放电SOC值

	float BalanceStartVoltage;	// 均衡起始电压(V)
	float BalanceDiffeVoltage;	// 均衡差异电压(V)
	uint32_t BalanceCycleTime;	// 均衡周期时间(s)
	BMS_CellIndexTypedef BalanceRecord;	// 均衡记录,正在均衡的会被位与上
	bool BalanceReleaseFlag;	// 表示均衡释放,false:表示已不满足均衡条件,true:满足均衡条件
}BMS_EnergyDataTypedef;

extern BMS_EnergyDataTypedef EnergyData;
void BMS_EnergyInit(void);

#endif
