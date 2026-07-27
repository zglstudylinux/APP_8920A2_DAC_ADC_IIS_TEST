#ifndef _BSP_ADC_AUX_EXT_H
#define _BSP_ADC_AUX_EXT_H

#include "include.h"

//A3 任务: AUX 改到 PB1/PB2 (避开 PE6/PE7 给 IIS LRC/DO 用)
//
//开发板可用引脚: PB1, PB2, PE4, PE5, PE6, PE7 (PA3-PA7 未引出)
//AUX 输入走 PB1/PB2 (CH_AUXL_PB1 | CH_AUXR_PB2 = 0x22)
//IIS_G2 输出走 PE5(BCLK) + PE6(LRC) + PE7(DO), MCLK 不输出
//
//A2 baseline (PE6/PE7) 保留不变, 由 test_aux_adc2dac() 负责
//A3 专用走线由 test_aux_adc2dac_for_a3() 负责

void test_aux_adc2dac(void);        //A2 baseline (PE6/PE7, 16K)
void test_aux_adc2dac_for_a3(void); //A3 专用 (PB1/PB2, 48K, DAC 48K)

#endif