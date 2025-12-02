#include "main.h"
#include "bq769x0.h"
#include "BMS_Monitor.h"


static void BMS_SortCellVoltage(BMS_CellDataTypedef* data, uint8_t len)
{
    // 外层循环：控制需要比较的轮次（n个元素需要n-1轮）
    for (uint8_t i = 0; i < len - 1; i++)
    {
        // 内层循环：每轮比较相邻元素，将较大的元素"冒泡"到后面
        // 优化：每轮结束后，最大的元素已到位，下一轮可减少一次比较
        for (uint8_t j = 0; j < len - 1 - i; j++)
        {
            // 若前一个元素的电压 > 后一个元素的电压，交换两者（从小到大排序）
            if (data[j].CellVoltage > data[j + 1].CellVoltage)
            {
                // 用临时变量保存前一个元素
                BMS_CellDataTypedef temp = data[j];
                // 交换两个元素（保持电压与编号的对应关系）
                data[j] = data[j + 1];
                data[j + 1] = temp;
            }
        }
    }
}

void BMS_MonitorVoltage_Itf(void)
{
	BQ769X0_UpdateCellVolt();
	for(uint8_t i=0;i<5;i++)
	{
		MonitorData.BMS_CellVoltage[i]=BQ769X0_SampleData.CellVoltage[i];
		MonitorData.BMS_CellData[i].CellVoltage=BQ769X0_SampleData.CellVoltage[i];
		MonitorData.BMS_CellData[i].CellNumber=i;
	}
	
	//要把数据传进去进行冒泡排序，得到排序好的结果
	BMS_SortCellVoltage(MonitorData.BMS_CellData,5);//从小到大排序
}

void BMS_MonitorTotolVoltage_Itf(void)
{
	float sum;
	for(uint8_t i=0;i<5;i++)
	{
		sum+=MonitorData.BMS_CellVoltage[i];
	}
	MonitorData.BMS_CellTotolVoltage=sum;
}

void BMS_MonitorCellTemp_Itf(void)
{
	BQ769X0_UpdateTsTemp();
	MonitorData.BMS_CellTemp=BQ769X0_SampleData.TsxTemperature;
}

void BMS_MonitorBatCurrent_Itf(void)
{
	BQ769X0_UpdateCurrent();
	MonitorData.BMS_BatCurrent=BQ769X0_SampleData.BatteryCurrent;
}
