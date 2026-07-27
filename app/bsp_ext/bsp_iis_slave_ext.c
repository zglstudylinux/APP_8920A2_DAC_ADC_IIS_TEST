#include "include.h"
#include "bsp_dac_ext.h"
#include "bsp_iis_ext.h"

//A4 任务: 在应用层重写 iis_slave_ram_rx_2_dac()
//
//使用 GCC --wrap 链接选项: 库代码里所有 iis_slave_ram_rx_2_dac() 调用
//会被改写到 __wrap_iis_slave_ram_rx_2_dac() (本函数)。
//
//A4 双板联调架构:
//   板 A (A3) PE5(BCLK) PE6(LRC) PE7(DO) ─── 4 根杜邦线 ─── 板 B (A4) PE5(BCLK) PE6(LRC) PB2(DI)
//   板 B IIS_G2 SLAVE_RAMRX DMA → iis_dmabuf1[2048] (.iis2dac_buf)
//   → IRQ_I2S_VECTOR → iis_isr_func → 库回调 iis_rx_process_test → AUBUFDATA → DAC 模拟输出
//
//★ 关键发现 ★
//   8920A2 SDK 的 sfr.h 没有定义 IISCON0 / IISBAUD 等寄存器 (530X 模板有)
//   所以用应用层 iis_cfg_init() 库函数, 它内部封装了所有寄存器配置
//   包括 IISCON0/IISBAUD/FUNCMCON2/CLKGAT/CLKCON + DMA + IRQ
//
//IO 映射 (IIS_G2, 8920A2 开发板可用引脚: PB1, PB2, PE4, PE5, PE6, PE7; PA3-PA7 未引出):
//   PE5 = IIS BCLK (输入, 来自板 A 主机)
//   PE6 = IIS LRC  (输入, 来自板 A 主机)
//   PB2 = IIS DI   (输入, 来自板 A 主机 PE7 DO)
//   PB1 = MCLK 位置, 关闭 MCLK + 关功能映射, 让 PB1 完全归 AUX-L 模拟

//库符号:无头文件原型,需要 extern 声明
//(反汇编 libplatform.a(bsp_iis_ext.o) 已确认符号类型 GLOBAL)
extern void iis_rx_process_test(void *buf, u32 samples, bool iis_32bit);
extern void aubuf_adjust(void);
extern u8 iis_dmabuf1[2048];

//A4: 用应用层 iis_cfg_init() 库函数 + 手配 GPIO/时钟
//     SLAVE_RAMRX 模式使用 iis_rx_process_test 库回调推 DAC (自带 aubuf_adjust 调速)
//
//★ 关键修复 ★
//   cfg 必须 static / 全局, 不能放栈上!
//   iis_cfg_init() 内部保存 cfg 指针 iis_libcfg, wrap() 返回后栈帧释放
//   iis_libcfg 指向已回收内存, ISR 触发 iis_rx_process_test 时访问它会读到
//   栈垃圾(0x2323...), 导致 ERR:3/7 EPC=0x23232324 反复重启
//   (库原版 iis_slave_ram_rx_2_dac 就是用 static iis_cfg_t iis_cfg; 反汇编已知)
AT(.com_text.iis_ext)
static iis_cfg_t iis_cfg_slave;     // ★ 必须 static, 不能放栈上!

void __wrap_iis_slave_ram_rx_2_dac(void)
{
    iis_cfg_t *cfg = &iis_cfg_slave;     //指向 static 变量,生命周期与程序同

    printf("\n--->%s (SLAVE_RAMRX via iis_cfg_init)\n", __func__);

    //-----------------------------------------------------------------------
    // 0) DAC 初始化 (跟 A3 板同步 44.1K)
    //   dac_init() 已在 main() 入口调用过, 这里只设音量/spr
    //-----------------------------------------------------------------------
    dac_spr_set(SPR_44100);    //与 A3 板对齐 44.1K
    dac_set_dvol(DIG_N0DB);    //0 dB 数字音量
    dac_set_avol(53);          //A2 PC12 调好的音量 (N_1DB)

    //-----------------------------------------------------------------------
    // 1) GPIO 预配: PE5/PE6/PB2 输入 + 弱下拉 (防浮空噪声)
    //   iis_cfg_init() 内部也会调 iis_io_init() 配这些脚, 我们先配一下保险
    //-----------------------------------------------------------------------
    //PE5 = BCLK 输入
    GPIOEDE |= BIT(5);
    GPIOEDIR |= BIT(5);                //input
    GPIOEPU  &= ~BIT(5);               //disable pull-up
    GPIOEFEN |= BIT(5);                //enable function mux

    //PE6 = LRC 输入
    GPIOEDE |= BIT(6);
    GPIOEDIR |= BIT(6);
    GPIOEPU  &= ~BIT(6);
    GPIOEFEN |= BIT(6);

    //PB2 = DI 输入
    GPIOBDE  |= BIT(2);
    GPIOBDIR |= BIT(2);
    GPIOBPU  &= ~BIT(2);
    GPIOBFEN |= BIT(2);

    //PB1 = MCLK 输出位置, slave 模式不输出 MCLK
    //   配 input + 不要功能映射, 让 PB1 完全归 AUX-L 模拟 (与 A3 板策略一致)
    GPIOBDE  |= BIT(1);
    GPIOBDIR |= BIT(1);        // input
    GPIOBPU  &= ~BIT(1);       // 不要上拉
    GPIOBFEN &= ~BIT(1);       // ★ 关键: 不让 PB1 走 IIS MCLK 功能

    //-----------------------------------------------------------------------
    // 2) 时钟门控 (库内部 iis_clk_set() 也会设, 提前开门)
    //-----------------------------------------------------------------------
    CLKGAT1 |= BIT(4);                         // IIS 模块时钟使能
    CLKGAT0 |= BIT(12);                        // IIS 时钟源使能

    //-----------------------------------------------------------------------
    // 3) 配 iis_cfg_t
    //   注意: dma_en = 0 (库原版就是 0, mode 已含 DMA 标志)
    //-----------------------------------------------------------------------
    memset(cfg, 0, sizeof(*cfg));                              //★ 清 struct 本身, 不是清指针
    cfg->mode        = IIS_SLAVE_RAMRX;        // 0x0A = IISCFG_RX|IISCFG_DMA (slave)
    cfg->iomap       = IIS_G2;                  // PE5=BCLK, PE6=LRC, PB2=DI (MCLK PB1 DIS)
    cfg->bit_mode    = IIS_16BIT;               // 16-bit
    cfg->data_mode   = IIS_DATA_NORMAL;         // IIS normal
    cfg->mclk_sel    = IIS_MCLK_256FS;          // 256fs
    cfg->mclk_out_en = IIS_MCLK_OUT_DIS;        // 不输出 MCLK
    cfg->dma_en      = 0;                       // ★ 库原版也是 0 (mode 已含 DMA 标志)

    //DMA 配置: 库默认 64 samples/buffer, 2048 bytes, 复用库内 bram
    cfg->dma_cfg.samples            = 64;
    cfg->dma_cfg.dmabuf_len         = 2048;
    cfg->dma_cfg.dmabuf_ptr         = iis_dmabuf1;             //库 .iis2dac_buf, ram.ld 已预留
    cfg->dma_cfg.iis_isr_rx_callbck = iis_rx_process_test;     //★ 库回调(自带 aubuf_adjust + 推 DAC FIFO + 1s 打印)
    cfg->dma_cfg.iis_isr_tx_callbck = NULL;                    //A4 只收不发

    iis_cfg_init(cfg);

    //-----------------------------------------------------------------------
    // 4) 启动 IIS
    //-----------------------------------------------------------------------
    iis_start();

    //-----------------------------------------------------------------------
    // 5) 调试打印
    //-----------------------------------------------------------------------
    printf("  iis_cfg: mode=IIS_SLAVE_RAMRX, iomap=IIS_G2, 16bit, 256fs, MCLK_DIS\n");
    printf("  dma_cfg: samples=64, dmabuf=0x%X (iis_dmabuf1), rx_cb=iis_rx_process_test\n",
           (u32)iis_dmabuf1);
    printf("  DACDIGCON0=0x%X (audio path alive)\n", DACDIGCON0);
    printf("  1s print: print_dac_info() 走库回调\n");
    printf("  Expect: PE5=BCLK in, PE6=LRC in, PB2=DI in from board A\n");
}