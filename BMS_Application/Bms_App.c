#include "stm32f10x.h"                  // Device header
#include "MyI2C.h"
#include "BMS_App.h"
#include "bq769x0.h"

#include "BMS_Monitor.h"
#include "BMS_Information.h"
#include "BMS_Protect.h"
#include "BMS_Anasys.h"
#include "BMS_Energy.h"
void BMS_SysInit(void)
{
	//硬件初始化，保护的部分我们最后写
	BQ769X0_InitDataTypedef InitData;
	
	//硬件保护数据填写
	InitData.AlertOps.cc	 = BMS_MonitorHwCurrent;//CC寄存器的中断，也就是CC寄存器可以读数时置1
	InitData.AlertOps.ocd  = BMS_ProtectHwOCD;
	InitData.AlertOps.scd  = BMS_ProtectHwSCD;
	InitData.AlertOps.ov	 = BMS_ProtectHwOV;
	InitData.AlertOps.uv 	 = BMS_ProtectHwUV;	
	// 这两个中断会造成系统故障
	// 第一个报警时设备故障,表示BQ芯片有问题了
	// 第二个报警可能存在被外界电磁信号干扰造成误判,之前出现过,换了个跟官方一样阻值的电阻就没出现过了
	InitData.AlertOps.device = BMS_ProtectHwDevice;
	InitData.AlertOps.ovrd 	 = BMS_ProtectHwOvrd;
	
	
	//硬件数据填写
	InitData.ConfigData.SCDDelay=Init_SCD_Delay;
	InitData.ConfigData.OCDDelay=Init_OCD_Delay;
	InitData.ConfigData.OVDelay=Init_OV_Delay;
	InitData.ConfigData.UVDelay=Init_UV_Delay;
	InitData.ConfigData.OVPThreshold=Init_OVPThreshold*1000;
	InitData.ConfigData.UVPThreshold=Init_UVPThreshold*1000;
	
	I2C_BusInitialize();
	BQ769X0_Init(&InitData);
	//软件初始化，我们主要的应用逻辑都是下面几个
	BMS_MonitorInit();	// 电池监控初始化,这个函数数我们用来进行电池数据的获取和电池状态的获取
	BMS_ProtectInit();	//电池保护初始化
	BMS_AnalysisInit(); 	//电池分析初始化
	BMS_EnergyInit();		//电池充放电和均衡管理初始化
	BMS_InfoInit(); 		//信息打印初始化
}
