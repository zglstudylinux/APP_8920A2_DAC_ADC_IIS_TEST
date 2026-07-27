#include "include.h"
#include "bsp_dac_ext.h"
#include "bsp_iis_ext.h"

//A3 任务: 在应用层重写 iis_master_srctx_init()
//
//使用 GCC --wrap 链接选项: 库代码里所有 iis_master_srctx_init() 调用
//会自动改写到 __wrap_iis_master_srctx_init() (本函数)。
//
//A3 整体架构 (A3+A4 双板联调, 两板都跑 44.1K 同步):
//   test_aux_adc2dac_for_a3()    →  AUX 音频 → SDADC → DMA → DAC FIFO → DAC 模拟输出
//   __wrap_iis_master_srctx_init() →  DAC FIFO 同步 → IIS (BCLK/LRC/DO) 数字输出
//
//★ 关键发现 ★
//   8920A2 SDK 的 sfr.h 没有定义 IISCON0 / IISBAUD 等寄存器 (530X 模板有)
//   所以用应用层 iis_cfg_init() 库函数, 它内部封装了所有寄存器配置
//   包括 IISCON0/IISBAUD/FUNCMCON2/CLKGAT/CLKCON + DACDIGCON0 BIT(23) SRCTX 使能
//
//IO 映射 (8920A2 开发板可用引脚: PB1, PB2, PE4, PE5, PE6, PE7; PA3-PA7 未引出):
//   PB1 = AUX-L 模拟输入 (PC2 已改 AUX 杜邦线到这里)
//   PB2 = AUX-R 模拟输入
//   PE5 = IIS BCLK (master 输出)        ← IIS_G2
//   PE6 = IIS LRC  (master 输出)        ← IIS_G2
//   PE7 = IIS DO   (master 输出)        ← IIS_G2
//   PB1 = IIS MCLK 位置, 关闭 MCLK 输出避免驱动 AUX-L 模拟脚

//A3 PC4: 用应用层 iis_cfg_init() 库函数 + 手配 GPIO/时钟
//       SRCTX 模式不要 DMA callback
AT(.com_text.iis_ext)
void __wrap_iis_master_srctx_init(void)
{
    iis_cfg_t cfg;

    printf("\n--->%s (PC4 SRCTX via iis_cfg_init)\n", __func__);

    //-----------------------------------------------------------------------
    // 1) GPIO pre-init: PE5/PE6/PE7 = output + high drive + no pull-up
    //   iis_cfg_init() 内部也会调 iis_io_init() 配这些脚, 我们先配一下保险
    //-----------------------------------------------------------------------
    GPIOEDE |= BIT(5) | BIT(6) | BIT(7);
    GPIOEFEN |= BIT(5) | BIT(6) | BIT(7);
    GPIOEDIR &= ~(BIT(5) | BIT(6) | BIT(7));
    GPIOEDRV |= BIT(5) | BIT(6) | BIT(7);
    GPIOEPU  &= ~(BIT(5) | BIT(6) | BIT(7));

    //PB1 = MCLK 输出位置, A3 关闭 MCLK → 配 input + 不要功能映射, 让 PB1 完全归 AUX-L 模拟
    GPIOBDE  |= BIT(1);
    GPIOBDIR |= BIT(1);        // input
    GPIOBPU  &= ~BIT(1);       // 不要上拉
    GPIOBFEN &= ~BIT(1);       // 关闭功能映射

    //-----------------------------------------------------------------------
    // 2) 时钟门控 (库内部 iis_clk_set() 也会设这些, 我们提前开门)
    //-----------------------------------------------------------------------
    CLKGAT1 |= BIT(4);                         // IIS 模块时钟使能
    CLKGAT0 |= BIT(12);                        // IIS 时钟源使能

    //-----------------------------------------------------------------------
    // 3) 配 iis_cfg_t 并调库函数 iis_cfg_init()
    //   库内部: iis_io_init() + iis_clk_set() + 清 pending + IISBAUD + IISCON0 + DACDIGCON0
    //-----------------------------------------------------------------------
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode        = IIS_MASTER_SRCTX;        // 主机 SRCTX 模式
    cfg.iomap       = IIS_G2;                  // PE5=BCLK PE6=LRC PE7=DO (MCLK on PB1 DIS)
    cfg.bit_mode    = IIS_16BIT;               // 16-bit
    cfg.data_mode   = IIS_DATA_NORMAL;         // IIS normal (left-justified 也可)
    cfg.mclk_sel    = IIS_MCLK_256FS;          // 256fs
    cfg.mclk_out_en = IIS_MCLK_OUT_DIS;        // 不输出 MCLK, 让 PB1 当 AUX-L 模拟脚
    cfg.dma_en      = 0;                       // SRCTX 走 DAC SRC buffer, 不需要 RAM DMA

    //SRCTX 模式不需要 DMA callback, dma_cfg 全 0
    iis_cfg_init(&cfg);

    //-----------------------------------------------------------------------
    // 4) 调用 iis_start() 启动 IIS (有些库在 iis_cfg_init 末尾会自动启动)
    //-----------------------------------------------------------------------
    iis_start();

    //-----------------------------------------------------------------------
    // 5) 调试打印: 寄存器状态 (注意: IISCON0/IISBAUD 地址未公开, 我们读不到)
    //   重点看 DACDIGCON0 BIT(23) 是否置位 = SRCTX 是否真正使能
    //-----------------------------------------------------------------------
    printf("  iis_cfg: mode=IIS_MASTER_SRCTX, iomap=IIS_G2, 16bit, 256fs, MCLK_DIS\n");
    printf("  DACDIGCON0=0x%X (BIT(23)=%d, SRCTX %s)\n",
           DACDIGCON0,
           (DACDIGCON0 & BIT(23)) ? 1 : 0,
           (DACDIGCON0 & BIT(23)) ? "ENABLED" : "DISABLED");
    printf("  Expect: PE5=BCLK@1.536MHz, PE6=LRC@48kHz, PE7=DO (sine data)\n");
}