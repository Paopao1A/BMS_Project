#include "main.h"
#include <stdio.h>
#include <rtthread.h>
#include "BMS_Monitor.h"
#include "BMS_Information.h"
#include "BMS_Anasys.h"
#define DBG_TAG "info"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"



static void BMS_InformationEntry(void* paramter);
static void BMS_InfoBatCapacityIndicator(void);

void BMS_InfoInit(void)
{
	rt_thread_t thread;
	thread=rt_thread_create("Information",BMS_InformationEntry,NULL,512,25,25);//信息打印的优先级最低
	
	if(thread==NULL)
	{
		LOG_E("Create Task Fail");
	}
	
	rt_thread_startup(thread);
}

static void BMS_InformationEntry(void* paramter)
{
	while(1)
	{
		BMS_InfoBatCapacityIndicator();//根据SOC值点亮LED灯
		//BMS_InfoPrint();//一直刷新数值很干扰观察，所以后续我们要写一下shell命令的添加，加一个shell命令用于调用这个函数打印信息
		rt_thread_delay(INFO_TASK_PERIOD);//INFO_TASK_PERIOD=2s(2000ms)
	}
}

static void BMS_InfoBatCapacityIndicator(void)
{
	if (BMS_AnalysisData.SOC == 0)
	{
		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);
	}
	else if (BMS_AnalysisData.SOC <= 0.25)
	{
		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);		
	}
	else if (BMS_AnalysisData.SOC <= 0.5)
	{
		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);	
	}
	else if (BMS_AnalysisData.SOC <= 0.75)
	{
		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);
	}
	else if (BMS_AnalysisData.SOC <= 1)
	{
		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);
	}
}

void BMS_InfoPrint(void)
{
	char str[64];//创造临时变量用于将数据转化为字符串发送打印
	LOG_D("/*************************************************************/");
	//打印电压信息
	for(uint8_t i=0;i<5;i++)
	{
		sprintf(str,"Cell%-2dVoltage =%-5.3fV",i,MonitorData.BMS_CellVoltage[i]);
		// -2表示占用两个宽度左对齐，-5.3表示占用五个宽度左对齐，保留三位小数
		LOG_D("%s",str);

	}
	
	//打印总电压
	sprintf(str,"TotolVoltage=%-6.3fV",MonitorData.BMS_CellTotolVoltage);
	LOG_D("%s",str);
	//rt_kprintf("\r\n");
	
	//打印最高电压的电芯信息
	sprintf(str,"Cell%-2dThe Highest Voltage=%-5.3fV",MonitorData.BMS_CellData[4].CellNumber,MonitorData.BMS_CellData[4].CellVoltage);
	LOG_D("%s",str);
	//rt_kprintf("\r\n");
	
	//打印电流信息
	sprintf(str,"Current=%-5.3fA",MonitorData.BMS_BatCurrent);
	LOG_D("%s",str);
	//rt_kprintf("\r\n");
	
	//打印温度信息
	sprintf(str,"Temp=%-5.3f°",MonitorData.BMS_CellTemp);
	LOG_D("%s",str);
	
	
	rt_kprintf("\r\n");
	
	
	// 电池包实际容量
	sprintf(str, "Battery Real Capacity = %0.3fAh", BMS_AnalysisData.CapacityReal);
	LOG_D("%s", str);

	// 电池包剩余容量
	sprintf(str, "Battery Remain Capacity = %0.3fAh", BMS_AnalysisData.CapacityRemain);
	LOG_D("%s", str);
	
	//打印当前SOC值
	sprintf(str,"SOC= %0.1f%%",BMS_AnalysisData.SOC*100);
	LOG_D("%s",str);
	
	LOG_D("/*************************************************************/");
	
	
	rt_kprintf("\r\n");
}