#include "include.h"
void dac_init(void);
void test_aux_adc2dac(void);
void test_pcm2dac(void);
void iis_master_srctx_init(void);
void iis_slave_ram_rx_2_dac(void);


AT(.com_text.iis_ext)
bool iis_slave_ram_rx_2_dac_1s_print_en(void)  //iis_slave_ram_rx_2_dac 函数中是否1S一次打印DAC信息(库中回调打印)。
{
    return true;
}

AT(.com_text.iis_ext)
bool test_aux_adc2dac_1s_print_en(void)      //test_aux_adc2dac 配置的AUX中断函数中是否1S一次打印DAC信息。//开打印有点打印噪音串到AUX
{
    return true;
}


const char *const test_mode_tbl[4]  = {
    "TEST_PCM2DAC",
    "TEST_AUX_ADC2DAC",
    "TEST_AUX_ADC2IISSRCTX",
    "TEST_IISRX2DAC",
};

//正常启动Main函数
int main(void)
{
    bsp_sys_init();
    printf("\n============================>xcfg_cb.test_mode[%d] = %s\n",xcfg_cb.test_mode,test_mode_tbl[xcfg_cb.test_mode]);
    if(xcfg_cb.test_mode >= TEST_MODE_MAX) {
        printf("xcfg_cb.test_mode config err\n");
        WDT_DIS(); while(1);
    }
    //DAC初始化
    dac_init();

    //A3+A4 双板联调: 强制切到测试模式 (烧录时根据板子切换下面两行)
    //   板 A (A3): xcfg_cb.test_mode = TEST_AUX_ADC2IISSRCTX;  // AUX → DAC + IIS SRCTX 输出 44.1K
    //   板 B (A4): xcfg_cb.test_mode = TEST_IISRX2DAC;         // IIS SLAVE RAMRX → DAC 接收 44.1K
    xcfg_cb.test_mode = TEST_AUX_ADC2IISSRCTX;   // ← 板 A 默认 (烧板 B 时改下一行)
//    xcfg_cb.test_mode = TEST_IISRX2DAC;        // ← 板 B 烧录时启用
    //测试模式(setting配置界面中选择)
    switch (xcfg_cb.test_mode) {
    case TEST_PCM2DAC:
        printf("TEST_PCM2DAC\n");
        //测试DAC SRCIN采样率是8K, 请把采样率改16K(dac_spr_set(SPR_16000))并推出500HZ, 1KHZ, 2KHZ 三种正弦波 (各用一个正弦波数组实现,并保留上传对应正弦波数组)
        test_pcm2dac();
        break;

    case TEST_AUX_ADC2DAC:
        printf("TEST_AUX_ADC2DAC\n");
        //aux中断推DAC函数 auxadc_pcm_to_dac 在库中,该函数需要在外面重写实现
        //aux电脑上播放正弦波,测试中断函数中,每1S有256BYTE ADC数据打印出来,使用VS2008编写程序把该打印数据转成pcm文件,拖入audacity或audition中,查看当前波形是否为正弦波
        test_aux_adc2dac();
        break;

    case TEST_AUX_ADC2IISSRCTX:
        printf("TEST_AUX_ADC2IISSRCTX (board A: AUX -> DAC + IIS SRCTX 44.1K)\n");
        test_aux_adc2dac_for_a3();  //A3: PB1/PB2 + DAC 44.1K + ADC 44.1K (与板 B 同步)
        //A3: __wrap_iis_master_srctx_init 走 DAC 内部 SRC buffer, BCLK PE5 + LRC PE6 + DO PE7
        //可以用逻辑分析仪抓取 PE5/PE6/PE7 三个 IO 口, 验证 IIS 输出
        iis_master_srctx_init();
        break;

    case TEST_IISRX2DAC:
        //A4: __wrap_iis_slave_ram_rx_2_dac 走 IIS RX DMA -> 库回调 iis_rx_process_test -> DAC
        //   BCLK PE5 + LRC PE6 + DI PB2 (来自板 A 的 PE5/PE6/PE7, 杜邦线交叉: PE7 -> PB2)
        //库函数 iis_slave_ram_rx_2_dac 由 GCC --wrap=iis_slave_ram_rx_2_dac 改写到本文件外
        //   app/bsp_ext/bsp_iis_slave_ext.c::__wrap_iis_slave_ram_rx_2_dac
        iis_slave_ram_rx_2_dac();
        printf("TEST_IISRX2DAC (board B: IIS SLAVE RAMRX -> DAC 44.1K)\n");
        break;

    default:
        break;
    }
    printf("Test End\n");
    WDT_DIS(); while(1);
    return 0;
}




