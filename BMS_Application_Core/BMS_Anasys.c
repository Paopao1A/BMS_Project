#include "main.h"
#include <rtthread.h>
#include "BMS_Anasys.h"
#include "BMS_Monitor.h"
#include <math.h>
#include "BMS_App.h"
#define DBG_TAG "Anasys"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

static void BMS_AnalysisTaskEntry(void *paramter);
static void BMS_AnalysisEasy(void);
static void BMS_AnalysisCalCap(void);
static void BMS_AnalysisSocCheck(void);
static void BMS_AnalysisCapAndSocInit(void);

// 三元锂电池 SOC 开路电压法计算数据表
uint16_t SocOcvTab[101]=
{
	3282, // 0%~1%	
	3309, 3334, 3357, 3378, 3398, 3417, 3434, 3449, 3464, 3477,	// 0%~10%
	3489, 3500, 3510, 3520, 3528, 3536, 3543, 3549, 3555, 3561,	// 11%~20%
	3566, 3571, 3575, 3579, 3583, 3586, 3590, 3593, 3596, 3599,	// 21%~30%
	3602, 3605, 3608, 3611, 3615, 3618, 3621, 3624, 3628, 3632,	// 31%~40%
	3636, 3640, 3644, 3648, 3653, 3658, 3663, 3668, 3674, 3679,	// 41%~50%
	3685, 3691, 3698, 3704, 3711, 3718, 3725, 3733, 3741, 3748,	// 51%~60%
	3756, 3765, 3773, 3782, 3791, 3800, 3809, 3818, 3827, 3837,	// 61%~70%
	3847, 3857, 3867, 3877, 3887, 3897, 3908, 3919, 3929, 3940,	// 71%~80%
	3951, 3962, 3973, 3985, 3996, 4008, 4019, 4031, 4043, 4055,	// 81%~90%
	4067, 4080, 4092, 4105, 4118, 4131, 4145, 4158, 4172, 4185,	// 91~100%
};

BMS_AnalysisDataTypedef BMS_AnalysisData=
{
	.CapacityRated=2.2,// 默认电池额定容量值(Ah)
										// 这个值没有实际用容量测仪校准过,是卖家口头说的
	.CapacityReal=20,//初始化为一个不可能出现的值
};

void BMS_AnalysisInit(void)
{

	rt_thread_t thread;

	thread = rt_thread_create("Anasys",BMS_AnalysisTaskEntry,NULL,256,21,25);

	if (thread == NULL)
	{
		LOG_E("Create Task Fail");
	}

	rt_thread_startup(thread);
}

static void BMS_AnalysisTaskEntry(void *paramter)
{
	BMS_AnalysisCapAndSocInit();
	while(1)
	{
		BMS_AnalysisEasy();//直接利用现有测量的值进行简单的计算
		BMS_AnalysisCalCap();//实时校准函数，设计温度等因素，这里我们就写一个温度因素
		BMS_AnalysisSocCheck();//SOC校验
		rt_thread_mdelay(ANALYSISI_TASK_PERIOD);
	}

}

static void BMS_AnalysisEasy(void)
{
	// 最大电压差
	BMS_AnalysisData.MaxVoltageDifference=MonitorData.BMS_CellData[4].CellVoltage-MonitorData.BMS_CellData[0].CellVoltage;
	
	// 平均电压
	BMS_AnalysisData.AverageVoltage=MonitorData.BMS_CellTotolVoltage/5;
	
	// 实时功率
	BMS_AnalysisData.PowerReal=MonitorData.BMS_CellTotolVoltage*MonitorData.BMS_BatCurrent;
	
	// 最大和最小电压
	BMS_AnalysisData.CellVoltMax=MonitorData.BMS_CellData[4].CellVoltage;
	BMS_AnalysisData.CellVoltMin=MonitorData.BMS_CellData[0].CellVoltage;
}

static void BMS_AnalysisCalCap(void)
{
	//实时校准容量涉及因素:温度、完整充放电、老化等等,这里我们暂时就实现温度因素
	//校准的是真实的电池容量
	static int16_t LastTemp=0;//最后一次校准温度比较值
	int16_t BMS_CellTemp=MonitorData.BMS_CellTemp*10;//化成整数，好计算
	uint8_t  Ratio;		//校准倍率
	uint16_t RateTemp;//电池容量的变化百分比
	
	//先判断一下是否需要进行校准，比如说每次轮询的温度差超过了1°，那我们就进入校准，否则直接退出函数
	
	if(BMS_CellTemp>LastTemp)
	{
		if((BMS_CellTemp-LastTemp)>10)
		{
			LastTemp=BMS_CellTemp;
		}
		else
			return;
	}
	else
	{
		if((LastTemp-BMS_CellTemp)>10)
		{
			LastTemp=BMS_CellTemp;
		}
		else
			return;
	}
	
	// 确定每一摄氏度的校准倍率
	// 该校准倍率的由来是根据不同温度下的放电曲线来的
	// 放电曲线：http://www.doczj.com/doc/1510977503.html
	// 上面链接的放电曲线跟这份代码的校准区间的参数有所不同	
	// 搜了几个三元锂电池的放电温度特性曲线,都是以25度常温为标准,25度时容量不受温度影响
		
	if (BMS_CellTemp >= 250)
	{
		// 温度大于25度时,每1度的倍率为0.001
		// 大于常温放电时间变长,就可以理解为容量增加
		// 增加的容量为：0.001 * (最小温度-常温)
		Ratio = 1;
	}
	else if (BMS_CellTemp  >= 100 && BMS_CellTemp < 250)//逻辑与，两个都成立才能执行。
	{
		// 温度小于25度时,每1度的倍率为0.002
		// 小于常温放电时间变短,就可以理解为容量减小
		// 减小的容量为：0.002 * (最小温度-常温)
		Ratio = 2;
	}
	else if (BMS_CellTemp  >= 0 && BMS_CellTemp < 100)
	{   
		Ratio = 3;
	}
	else if (BMS_CellTemp >= -200 && BMS_CellTemp < -10)
	{   
		Ratio = 4;
	}
	else if (BMS_CellTemp  >= -300 && BMS_CellTemp < -200)
	{   
		Ratio = 5;
	}
	else
	{
        // ratio:这个量表示是一个变化趋势，温度越低，温度区间范围越大，也就证明变化趋势越大，所以ratio也在增大
		Ratio = 6;
	}
	
	// 该公式理解:
	// 1000：表示为电池容量为100%
	// ratio：表示为特定温度区间内,每一摄氏度容量衰减/增加的倍率
	// (BMS_CellTemp - 250) / 10:高/低了多少度
	// RateTemp:计算出来的就是电池衰减/增加百分比
	RateTemp=1000+Ratio*(BMS_CellTemp - 250) / 10;
	
	//计算实际的容量
	BMS_AnalysisData.CapacityReal=BMS_AnalysisData.CapacityRated*RateTemp/1000;//浮点数和整数相乘的时候会自动把整数转化为浮点数，所以没有除1000.0，除1000是因为1000代表百分之百
	
}

static uint16_t	BMS_SocCulculate(uint16_t Voltage)
{
	uint16_t soc=0;
	uint8_t i=0;//用于轮询电压值在数组的位置
	
	while(i<100&&Voltage>SocOcvTab[i])//防止溢出
	{
		i++;//计算出电压比较的右值
	}
		
	if(Voltage<=SocOcvTab[0])
	{
		soc=0;
	}
	else if(Voltage>=SocOcvTab[100])
	{
		soc=1000;//百分之百
	}
	else
	{
		if(Voltage==SocOcvTab[i])
		{
			soc=i*10;
		}
		else
		{//乘10是为了补充精度。比如说我现在i是50，后面计算出1/3=0.3四舍五入是0，那么最后soc值就是50%，精度太差
			//乘10，i是500，后面计算1/3*10=3.3，注意乘法在前面，如果*10写在最后，前面整数相处导致四舍五入了
			//四舍五入3，i=503，再除1000就是0.503，50.3%精度变高
			soc=(i-1)*10+((Voltage-SocOcvTab[i-1])*10)/(SocOcvTab[i]-SocOcvTab[i-1]);
			//其实这个得做一个防止除0崩溃，可能左界限等于右界限，不过表格没有相等的值，就不写了
		}
	}
	return soc;
}

static void	BMS_AnalysisOcvSocCalculate(void)
{
	if(Globle_State.Sysmod==BMS_MODE_SLEEP)//只有我们进入睡眠模式才进行开路电压检测
	{
		// 等待一段时间电压平稳,防止均衡才刚结束
		rt_thread_mdelay(5000);
		
		//计算开路SOC
		BMS_AnalysisData.SOC=BMS_SocCulculate(MonitorData.BMS_CellData[0].CellVoltage*1000)/1000.0;
		
		//计算剩余的容量
		BMS_AnalysisData.CapacityRemain=BMS_AnalysisData.CapacityReal*BMS_AnalysisData.SOC;
	}
	
}

static void	BMS_AnalysisAHSocCalculate(void)//安时积分法
{
	float CurrentValue=fabs(MonitorData.BMS_BatCurrent)/3600; //fabs可以对浮点数取绝对值，除3600转化为Ah
	
	if(Globle_State.Sysmod==BMS_MODE_STANDBY)//检查一下待机模式，如果电压超过了保护电压，认定此时SOC为1
	{
		if(MonitorData.BMS_CellData[0].CellVoltage>=TLB_OV_PROTECT)
		{
			BMS_AnalysisData.SOC=1;
		}
		else if(MonitorData.BMS_CellData[0].CellVoltage<=TLB_UV_PROTECT)
		{
			BMS_AnalysisData.SOC=0;
		}
	}

  if(Globle_State.Sysmod==BMS_MODE_CHARGE)
  {
		if(BMS_AnalysisData.CapacityReal>=(BMS_AnalysisData.CapacityRemain+CurrentValue))
		{
			BMS_AnalysisData.CapacityRemain+=CurrentValue;
		}
		else
		{
			BMS_AnalysisData.CapacityRemain=BMS_AnalysisData.CapacityReal;//如果下一次再充已经超过了实际，那就等与实际值
		}
  }
  else if(Globle_State.Sysmod==BMS_MODE_DISCHARGE)
  {
		if(BMS_AnalysisData.CapacityRemain>CurrentValue)//如果剩余值一直大于基准，那就减掉，直到减到0
		{
			BMS_AnalysisData.CapacityRemain-=CurrentValue;
		}
		else
		{
			BMS_AnalysisData.CapacityRemain=0;
		}
  }
	
	//更新SOC
	BMS_AnalysisData.SOC=BMS_AnalysisData.CapacityRemain/BMS_AnalysisData.CapacityReal;
	
}


static void BMS_AnalysisSocCheck(void)
{
	BMS_AnalysisOcvSocCalculate();//开路电压法，主要用于开机的soc值计算
	BMS_AnalysisAHSocCalculate();//安时积分法，主要用于工作时的soc值计算
}

// 容量和SOC上电初始化,这个是有必要的，上电的时候进行一次开路SOC计算
static void BMS_AnalysisCapAndSocInit(void)
{
	// soc计算
	BMS_AnalysisData.SOC = BMS_SocCulculate(MonitorData.BMS_CellData[0].CellVoltage  * 1000) / 1000.0;

	// 实际容量后面再完善,涉及到完整充放电流计算、老化损耗、温度特性曲线、信息存储模块
	//这里可以进行优化，我们没必要每次上电都把实际值覆盖成理论值，我们可以检测这是不是第一次上电
	//即检测BMS_AnalysisData.CapacityReal是否存在历史值，是的话用理论值覆盖，有历史值就不用覆盖了
	//得用到FLASH区的存储扇区，然后用CRC校验使数据稳定，不过太麻烦了
	//或者直接简单粗暴，初始化BMS_AnalysisData.CapacityReal为一个不可能出现的值，起始判断一下就行了,后面一直用校准后的真实值就行
	if(BMS_AnalysisData.CapacityReal==20)
	{
		BMS_AnalysisData.CapacityReal = BMS_AnalysisData.CapacityRated;
	}
	// 剩余容量 = 实际容量 * soc
	BMS_AnalysisData.CapacityRemain = BMS_AnalysisData.CapacityReal * BMS_AnalysisData.SOC; 
}
