#include <rtthread.h>
#include "main.h"

#include "BMS_Information.h"
#include "BMS_Monitor.h"
#include "bq769x0.h"

#define DBG_TAG "cmd"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

static void BMS_InfoCmd(void)//打印函数
{
	BMS_InfoPrint();
}
MSH_CMD_EXPORT(BMS_InfoCmd, Print Info);


static void BMS_CmdOpenDSG(void)//使能放电
{
	Globle_State.Discharge=BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenDSG, Open DSG);


static void BMS_CmdCloseDSG(void)//失能放电
{
	Globle_State.Discharge=BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseDSG, Close CHG);


static void BMS_CmdOpenCHG(void)//使能充电
{
	Globle_State.Charge=BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenCHG, Open DSG);


static void BMS_CmdCloseCHG(void)//失能充电
{
	Globle_State.Charge=BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseCHG, Close CHG);


static void BMS_CmdOpenBalance(void)//使能均衡
{
	Globle_State.Balance = BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenBalance, Open Balance);



static void BMS_CmdCloseBalance(void)//失能均衡
{
	Globle_State.Balance = BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseBalance, Close Balance);


static void BMS_CmdLoadDetect(void)//检查是否有外部载荷
{
	if(BQ769X0_LoadDetect()==true)
	{
		LOG_I("Load Detected");
	}
	else
	{
		LOG_I("No Load Was Detected");
	}
}
MSH_CMD_EXPORT(BMS_CmdLoadDetect, Load Detect);
