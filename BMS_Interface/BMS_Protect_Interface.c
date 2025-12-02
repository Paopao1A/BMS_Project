#include "main.h"
#include "bq769x0.h"

void BMS_CtrlCharge_Interface(BQ769X0_StateTypedef NewState)
{
	BQ769X0_ControlDSGOrCHG(NewState,CHG_CONTROL);
}

void BMS_CtrlDischarge_Interface(BQ769X0_StateTypedef NewState)
{
	BQ769X0_ControlDSGOrCHG(NewState,DSG_CONTROL );
}