#ifndef __BMS_ANASYS_H__
#define __BMS_ANASYS_H__

#define ANALYSISI_TASK_PERIOD		1000//执行周期

typedef struct
{
	
	float SOC;//// 电池包SOC值(剩余电量百分比)

	float AverageVoltage;		// 单体平均电压值(V)
	float MaxVoltageDifference;	// 单体电芯最大电压差(V)
	float PowerReal;			// 电池包实时功率(W)
	float CellVoltMax;			// 单体电芯最大电压(V)
	float CellVoltMin;			// 单体电芯最小电压(V)
	
	float CapacityRated;		// 电池包额定容量(Ah)
	float CapacityReal;			// 电池包实际容量(Ah)  		计算方法得进行一次完整的充放电计算
	float CapacityRemain;		// 电池包剩余容量(Ah)
}BMS_AnalysisDataTypedef;

extern BMS_AnalysisDataTypedef BMS_AnalysisData;
void BMS_AnalysisInit(void);

#endif
