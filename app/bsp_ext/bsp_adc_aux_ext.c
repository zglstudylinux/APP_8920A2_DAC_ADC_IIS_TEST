#include "include.h"
#include "bsp_dac_ext.h"
#define SDADC_DMA_SIZE           256         //DMASIZE = 鏈€澶ф牱鐐规暟
bool test_aux_adc2dac_1s_print_en(void);
void auxadc_pcm_to_dac(u8 flag, u8 *adc_buf, u16 adc_samples);
typedef struct {
    u16 channel;
    u16 sample_rate;
    u16 gain;        //浣?bit涓簊dadc digital gain, 鍏跺畠bit涓烘ā鎷焔ain鎺у埗
    u16 samples;
    u8 *buf;
} auxadc_cb_t;

auxadc_cb_t auxadc_cb;

AT(.com_text.isr.sdadcprint)
const char str_adcisr[] = "[%X]";
AT(.com_text.sdadcprint)
const char str_dacinfo[] = "DAC FIFOCNT=%d, AUBUFSIZE=%d, DVOL=0x%X, AVOL=0x%X, PHASECOM=0x%X, DACDIGCON0=0x%X\n";

AT(.com_text.dac_info)
void print_dac_info(void)
{
    my_printf(str_dacinfo,AUBUFFIFOCNT&0xFFFF,(u16)AUBUFSIZE,DACVOLCON&0x7FFF,AUANGCON3 &0x7F,PHASECOMP,DACDIGCON0);
}

AT(.com_text.isr.sdadc)
void auxadc_isr(void)
{
    //printf(str_adcisr,SDADCDMAFLAG&0x0F);
    if (SDADCDMAFLAG & BIT(1)) {                    //LDma Half Done
        SDADCDMACLR = BIT(1);
        auxadc_pcm_to_dac(1,auxadc_cb.buf, auxadc_cb.samples);   //璇ュ嚱鏁板湪搴撲腑,绉绘鏃堕渶瑕佸湪澶栭潰閲嶆柊鍐欎竴涓潵瀹炵幇
    } else if (SDADCDMAFLAG & BIT(0)) {             //LDma Done
        SDADCDMACLR = BIT(0);
        auxadc_pcm_to_dac(0,auxadc_cb.buf, auxadc_cb.samples);   //璇ュ嚱鏁板湪搴撲腑,绉绘鏃堕渶瑕佸湪澶栭潰閲嶆柊鍐欎竴涓潵瀹炵幇
    }

//    if (SDADCDMAFLAG & BIT(3)) {                    //RDma Half Done
//        SDADCDMACLR = BIT(3);
//    } else if (SDADCDMAFLAG & BIT(2)) {             //RDma Done
//        SDADCDMACLR = BIT(2);
//    }

    static u32 ticks = 0;
    if (tick_check_expire(ticks,1000)) {  //A2 PC10: 重新启用 ISR 串口打印 (诊断)  //1S鎵撳嵃涓€娆℃暟鎹?
        ticks = tick_get();
        if (test_aux_adc2dac_1s_print_en()) {
            print_dac_info();
            print_r(auxadc_cb.buf,256);  //鏌ョ湅ADC buf涓殑鏁版嵁,鍙互鎾斁姝ｅ鸡娉?鎶婅鏁版嵁杞垚bin鎴杙cm鏂囦欢,鎷栧叆audacity杞腑鏌ョ湅鏄惁鏄寮︽尝
        }
    }
}


void auxadc_irq_init(void)
{
    register_isr(IRQ_SDADC_VECTOR, auxadc_isr);
    PICPR &= ~BIT(IRQ_SDADC_VECTOR);                   //low priority interrupt
	PICEN |= BIT(IRQ_SDADC_VECTOR);
}

AT(.text.sdadc.table)
const u8 auxadc_spr_index_tbl[] = {0, 0, 0, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8};

AT(.text.sdadc)
void auxadc_spr_set(u16 spr)
{
    u32 index = 0;
    u16 ch = spr & 0x1c0;
    spr = spr & 0x0f;
    if ((spr == SPR_352800) || (spr == SPR_176400) || (spr == SPR_88200) || (spr == SPR_44100) || (spr == SPR_22050) || (spr == SPR_11025)) {
        adpll_spr_set(0);
    } else {
        adpll_spr_set(1);
    }
    index = auxadc_spr_index_tbl[spr];
    if (ch & 0x40) {
        //宸﹀０閬揝DADC Channel sample rate閰嶇疆
        SDADCDIGCON &= ~(0x0f << 3);
        SDADCDIGCON |= (index << 3);
    }
    if (ch & 0x80) {
        //鍙冲０閬揝DADC Channel sample rate閰嶇疆
        SDADCDIGCON &= ~(0x0f << 11);
        SDADCDIGCON |= (index << 11);
    }
}

//0~31 : 0~3 DB step 3/32 DB
AT(.text.sdadc)
void auxadc_set_digital_gain(u16 gain)
{
    u16 ch = gain & 0x1c0;
    u32 index = gain & 0x3f;
    if (ch & 0x40) {
        //left sdadc digital gain setting
        SDADCGAINCON &= ~0x3f;
        SDADCGAINCON |= index;
    }
    if (ch & 0x80) {
        //right sdadc digital gain setting
        SDADCGAINCON &= ~(0x3f << 16);
        SDADCGAINCON |= (index << 16);
    }
}


u32 buf_auxadc[512];

AT(.text.sdadc)
void auxadc_param_init(void)  //鍙傛暟鍒濆鍖?
{
    printf("%s\n",__func__);
    memset(&auxadc_cb, 0, sizeof(auxadc_cb));
    auxadc_cb.buf = (u8 *)&buf_auxadc[0];      //澶嶇敤DAC鎵╁睍BUF 512bytes
    //A2 任务: AUX 改 PB1/PB2 (避开 PE6/PE7 给 IIS 用, 走 PB1/PB2 测试 DAC 输出)
    //   PB1 -> AUXL (CH_AUXL_PB1)
    //   PB2 -> AUXR (CH_AUXR_PB2)
    //注意: 原代码默认的 PA6/PA7 实际上不是 AUX 输入
    auxadc_cb.channel = CH_AUXL_PB1 | CH_AUXR_PB2;  // 0x02 | 0x20 = 0x22
    auxadc_cb.sample_rate = SPR_16000;  //A2 PC11: ADC 与 DAC 同步 16kHz, 不走 SRC;
    auxadc_cb.samples = 512;    //A2 PC12: 512个样点 (降低 ADC ISR 频率)
    auxadc_cb.gain = (8 << 6) | (15);    //A2 降噪: 模拟增益 8 (约 -3 dB), 数字增益 15 (约 -15 dB)
    auxadc_irq_init();
}

AT(.text.sdadc)
int auxadc_digital_init(void)
{
    printf("%s\n",__func__);
    if (auxadc_cb.channel  == 0) {
        return -1;
    }
    //dac_spr = auxadc_cb.sample_rate;
    //p_ch = &auxadc_cb.left;
    printf("auxadc_cb.channel  = 0x%X\n",auxadc_cb.channel);
    if ((auxadc_cb.channel  & CHANNEL_L) && (auxadc_cb.channel  & CHANNEL_R)) {
        //鍙屽０閬揝tereo
        printf("dual,dmabuf = 0x%X, dmasize = %d\n",auxadc_cb.buf, auxadc_cb.samples);
        SDADCLDMAADDR = DMA_ADR(auxadc_cb.buf);      //Left  Channel DMA begin Address
        SDADCLDMASIZE = auxadc_cb.samples - 1;               //Left  Channel DMA Samples Number : DMASize +1 (half word)
        SDADCDMACON  = 0;
        SDADCDIGCON  |= 0x0202;                         //left & right remove dc enable
        auxadc_spr_set(auxadc_cb.sample_rate | 0xc0);     //setting sdadc sdadc sample rate
        SDADCDIGCON  |= 0x0404;                         //left & right channel Digital Gain enable
        auxadc_set_digital_gain((u8)(auxadc_cb.gain & 0x3f) | 0xc0);  //setting sdadc left & right digital gain
    } else if (auxadc_cb.channel  & CHANNEL_L){
        //宸﹀０閬?
        SDADCLDMAADDR = DMA_ADR(auxadc_cb.buf);          //Left  Channel DMA begin Address
        SDADCLDMASIZE = auxadc_cb.samples - 1;                   //Left  Channel DMA Samples Number : DMASize +1 (half word)
        SDADCDMACON  &= ~0x31;
        SDADCDMACON  |= BIT(3);                         //Left channel DMA to left address, right channel DMA to right Address
        SDADCDIGCON  |= 0x02;                           //left remove dc enable
        auxadc_spr_set(auxadc_cb.sample_rate | 0x40);     //setting left sdadc sample rate
        SDADCDIGCON  |= 0x04;                           //Left channel Digital Gain enable
        auxadc_set_digital_gain((u8)(auxadc_cb.gain & 0x3f) | 0x40);  //setting sdadc left digital gain
    } else if (auxadc_cb.channel  & CHANNEL_R) {
        //鍙冲０閬?
        //p_ch = &auxadc_cb.right;
        SDADCRDMAADDR = DMA_ADR(auxadc_cb.buf + SDADC_DMA_SIZE * 2); //Right Channel DMA begin Address
        SDADCRDMASIZE = auxadc_cb.samples - 1;               //Right Channel DMA Samples Number : DMASize +1 (half word)
        SDADCDMACON  &= ~0xc2;
        SDADCDMACON  |= BIT(3);                     //Left channel DMA to left address, right channel DMA to right Address
        SDADCDIGCON  |= 0x0200;                         //Right remove dc enable
        auxadc_spr_set(auxadc_cb.sample_rate | 0x80);     //setting right sdadc sample rate
        SDADCDIGCON  |= 0x0400;                         //right channel Digital Gain enable
        auxadc_set_digital_gain((u8)(auxadc_cb.gain & 0x3f) | 0x80);  //setting sdadc right digital gain
    }
    printf("%s ok\n",__func__);
    return 0;
}


AT(.text.aux)
void aux_analog_channel_select_ext(u8 channel)
{
    u8 left = channel & CHANNEL_L;
    u8 right = channel & CHANNEL_R;

    if (left) {
        if (left == CH_AUXL_PA6) {
            AUANGCON7 |= BIT(8);                    //AUX left channel input source 0 (PA6)
        }
        else if (left == CH_AUXL_PB1) {
            AUANGCON7 |= BIT(9);                    //AUX left channel input source 1 (PB1)
        }
        else if (left == CH_AUXL_PE6) {
            AUANGCON7 |= BIT(10);                   //AUX left channel input source 2 (PE6/AUXL2)
        }
        else if (left == CH_AUXL_PF4) {
            AUANGCON7 |= BIT(11);                   //AUX left channel input source 3 (PF4)
        }
        else if (left == CH_AUXL_VCMBUF) {
            AUANGCON7 |= BIT(19);                   //Enable bit for 'VCMBUF_VOUTLN' PAD being an input of AUX left channel
        }
        else if (left == CH_AUXL_VOUTRP) {
            AUANGCON7 |= BIT(15);                    //Enable bit for 'VOUTR_P' PAD being an input source of AUX left channel
        }
    }
    if (right) {
        if (right == CH_AUXR_PA7) {
            AUANGCON8 |= BIT(8);                   //AUX right channel input source 0
        }
        else if (right == CH_AUXR_PB2) {
            AUANGCON8 |= BIT(9);                   //AUX right channel input source 1 (PB2)
        }
        else if (right == CH_AUXR_PE7) {
            AUANGCON8 |= BIT(10);                  //AUX right channel input source 2 (PE7/AUXR2)
        }
        else if (right == CH_AUXR_PF5) {
            AUANGCON8 |= BIT(11);                   //AUX left channel input source 3 (PF5)
        }
        else if (right == CH_AUXR_VCMBUF) {
            AUANGCON8 |= BIT(19);                   //Enable bit for 'VCMBUF_VOUTLN' PAD being an input of AUX right channel
        }
        else if (right == CH_AUXR_VOUTRN) {
            AUANGCON8 |= BIT(15);                   //Enable bit for 'VOUTR_P' PAD being an input source of AUX right channel
        }
    }
}

AT(.text.aux)
void set_aux_analog_gain_ext(u8 level, u8 channel)
{
    u32 aux_gain = ((level << 2) | 0x03) & 0x3f;

    AUANGCON7 = (AUANGCON7 & ~(0x3f << 2)) | ((u32)aux_gain << 2);      //AUXLGC, AUXLGX;
    if (channel & CHANNEL_R) {
        AUANGCON8 = (AUANGCON8 & ~(0x3f << 2)) | ((u32)aux_gain << 2);  //AUXRGC, AUXRGX;
    }
    //printf("%s(gain: %d, channel: %02x, AUANGCON5: %x)\n", __func__, level, channel, AUANGCON5);
}


//AUX -> sdadc
AT(.text.aux) WEAK
void auxadc_analog_aux_start(u8 channel, u8 gain)
{
    //AUANGCON0 |= BIT(9);                        //DI_EN_VCM = 1   //dac_cb.type == DAC_DIFF_MONO, DAC_DIFF_DUAL
    AUANGCON2 |= BIT(0);                            //DI_MICADC_BIASTOP, MICADC bias enable
    AUANGCON2 |= BIT(2);                            //DI_MICAUD_ENMICBIAS
    AUANGCON2 |= BIT(24);                           //MICAUD_VCMBUFBCEN
    AUANGCON2 |= BIT(23);                           //MICAUD_VCMBUF
    aux_analog_channel_select_ext(channel);             //aux channel select
    set_aux_analog_gain_ext(gain, channel);             //set aux analog gain
    if (channel & CHANNEL_L) {
        AUANGCON7 |= BIT(0);                        //DI_MICAUD_ENAUXL, Master enable bit for left channel AUX chain
        AUANGCON4 |= BIT(6);                        //DI_CTADCL_ENADCBIAS
        AUANGCON4 |= BIT(11);                       //DI_CTADCL_ENDAC
        AUANGCON7 |= BIT(26);                       //MICAUDL_ISELS2DBUF
        AUANGCON7 |= BIT(20);                       //MICAUDL_ENS2DBUF
        AUANGCON4 |= BIT(7);                        //CTADCL_ENCHAN1
        AUANGCON4 &= ~BIT(8);                       //CTADCL_ENCHAN2
        AUANGCON7 |= BIT(23);                       //DI_MICAUDL_AUXL2ADC, select AUXL output signal to s2dbuf
        AUANGCON7 &= ~BIT(24);                      //DISABLE AUXL->AUXR2ADC
        AUANGCON7 &= ~BIT(1);                       //AUXL channel mute diable
//      AUANGCON4 |= BIT(13);                       //DI_CTADCL_LPADC,  ADCL lowpower mode
        AUANGCON4 &= ~BIT(4);                       //CTADCL_CTRIM0
        AUANGCON4 |= BIT(3);                        //CTADCL_CTRIM1
        AUANGCON4 |= BIT(5);                        //CTADCL_ENADC
        AUANGCON4 |= BIT(9);                        //CTADCL_ENCK
//      DI_CTADCL_LPADC(0x0);//

    }
    if (channel & CHANNEL_R) {
        AUANGCON8 |= BIT(0);                        //DI_MICAUD_ENAUXR, Master enable bit for right channel AUX chain
        AUANGCON6 |= BIT(6);                        //DI_CTADCR_ENADCBIAS
        AUANGCON6 |= BIT(11);                       //DI_CTADCR_ENDAC
        AUANGCON8 |= BIT(26);                       //MICAUDR_ISELS2DBUF
        AUANGCON8 |= BIT(20);                       //MICAUDR_ENS2DBUF
        AUANGCON6 |= BIT(7);                        //CTADCR_ENCHAN1
        AUANGCON6 &= ~BIT(8);                       //CTADCR_ENCHAN2
        AUANGCON8 |= BIT(24);                       //DI_MICAUDR_AUXR2ADC, select AUXR output signal to s2dbuf
        AUANGCON8 &= ~BIT(23);                      //DISABLE AUXR->AUXL2ADC
        AUANGCON8 &= ~BIT(1);                       //AUXR channel mute disable
//        AUANGCON6 |= BIT(13);                       //DI_CTADCR_LPADC,  ADCR lowpower mode
        AUANGCON6 &= ~BIT(4);                       //CTADCR_CTRIM0
        AUANGCON6 |= BIT(3);                        //CTADCR_CTRIM1
        AUANGCON6 |= BIT(5);                        //CTADCR_ENADC
        AUANGCON6 |= BIT(9);                        //CTADCR_ENCK
    }
}

///鍒濆鍖朣DADC瀵瑰簲channel鐨勬ā鎷熴€佹暟瀛楅厤缃?
AT(.text.sdadc)
int auxadc_analog_init(void)
{
    u8 gain;
    u16 channel = auxadc_cb.channel;
    printf("%s\n",__func__);
    gain = (u8)(auxadc_cb.gain >> 6);                //analog gain
    if ((channel & CHANNEL_L) && (channel & CHANNEL_R)) {
        if ((channel != auxadc_cb.channel) || (SDADCDMACON & BIT(3))) {
            return -2;
        }
        //stereo analog configure
        auxadc_analog_aux_start(auxadc_cb.channel, gain);
        SDADCDMACON |= 0x03;                        //left & right dma enable; Left, Right stereo mode
        SDADCDMACON |= 0x30;                        //enable left dma half & all done interrupt
        SDADCDIGCON |= 0x0101;                          //left & right channel sdadc enable
    } else if (channel & CHANNEL_L) {
        //left channel sdadc source analog configure
        auxadc_analog_aux_start(auxadc_cb.channel, gain);
        SDADCDMACON |= 0x09;                        //Left  Channel DMA enable
        SDADCDMACON |= 0x30;                        //enable left dma half & all done interrupt
        SDADCDIGCON |= 0x01;                        //left channel sdadc enable
    } else if (channel & CHANNEL_R) {
        if ((channel & CHANNEL_R) != (auxadc_cb.channel & CHANNEL_R)) {
            return -4;
        }
        //right channel sdadc source analog configure
        auxadc_analog_aux_start(auxadc_cb.channel & CHANNEL_R, gain);
        SDADCDMACON |= 0x0a;                        //Right Channel DMA enable
        SDADCDMACON |= 0xc0;                        //enable right dma half & all done interrupt
        SDADCDIGCON |= 0x0100;                          //Right channel sdadc enable
    }
    printf("%s ok\n",__func__);
    return 0;
}


void test_aux_adc2dac(void)
{
    dac_spr_set(SPR_16000);  //A2 PC11: DAC 16kHz + internal SRC閲囨牱鐜?8~48K鍙€?
    dac_set_dvol(DIG_N0DB);  //璁剧疆鏁板瓧闊抽噺,鏈€澶?DB
    dac_set_avol(53);        //A2 PC12: avol=53 (N_1DB ~ -1 dB) 接近上限: avol=50 (N_4DB ~ -4 dB) 接近上限, 声音大

    auxadc_param_init();    //閲囨牱鐜?澧炵泭锛孉DCBUF,涓柇绛夊弬鏁拌缃?
    auxadc_digital_init();  //ADC 鏁板瓧閮ㄤ唤閰嶇疆
    auxadc_analog_init();   //ADC 妯℃嫙/閫氶亾绛夐厤缃?
    //閰嶇疆瀹屾垚鐨?浼氬湪ADC涓柇涓帹DAC
}

//A3 任务: 复用 test_aux_adc2dac() 的结构, 但是改 PB1/PB2 + 48K
//PC2: DAC 切到 48K (SRCTX 必须), ADC 同步 48K (不经 SRC 上采, 减少噪声)
AT(.text.aux)
void auxadc_param_init_for_a3(void)
{
    printf("%s\n",__func__);
    memset(&auxadc_cb, 0, sizeof(auxadc_cb));
    auxadc_cb.buf = (u8 *)&buf_auxadc[0];
    //A3 关键改动: AUX 改 PB1/PB2, 避开 PE6/PE7 (留给 IIS LRC/DO)
    auxadc_cb.channel = CH_AUXL_PB1 | CH_AUXR_PB2;  //0x02 | 0x20 = 0x22
    //A3 关键改动: ADC 采样率 48K, 与 DAC 48K 同步 (不经 SRC)
    auxadc_cb.sample_rate = SPR_48000;
    //A2 调好的参数保留: samples=512, gain=(8<<6)|15
    auxadc_cb.samples = 512;
    auxadc_cb.gain = (8 << 6) | (15);
    auxadc_irq_init();
}

AT(.text.aux)
void test_aux_adc2dac_for_a3(void)
{
    //DAC 输出采样率 48K (SRCTX 要求 44.1K / 48K)
    dac_spr_set(SPR_48000);
    dac_set_dvol(DIG_N0DB);
    dac_set_avol(53);     //A2 PC12 调好的音量
    auxadc_param_init_for_a3();  //PB1/PB2 + 48K
    auxadc_digital_init();
    auxadc_analog_init();
}







