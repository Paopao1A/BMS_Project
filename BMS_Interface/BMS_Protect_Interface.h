#ifndef __BMS_PROTECT_INTERFACE_H__
#define __BMS_PROTECT_INTERFACEH__

#include "bq769x0.h"

void BMS_CtrlCharge_Interface(BQ769X0_StateTypedef NewState);
void BMS_CtrlDischarge_Interface(BQ769X0_StateTypedef NewState);

#endif
