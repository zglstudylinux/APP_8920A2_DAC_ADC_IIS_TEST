#include "include.h"
#include "bsp_dac_ext.h"

//A2 任务: 在应用层重写 auxadc_pcm_to_dac()
//
//使用 GCC --wrap 链接选项: 库代码里所有 auxadc_pcm_to_dac 调用
//会自动改写到 __wrap_auxadc_pcm_to_dac (本函数)。
//
//DAC FIFO 状态:
//   AUBUFCON BIT(8) = 1 表示 FIFO 已满, BIT(8) = 0 表示未满可写
//   AUBUFCON BIT(0) = 1 表示 FIFO reset (冻结, 不消费)
//   dac_obuf_init() 写了 BIT(0)=1, 必须写 0 退出 reset 才能消费
//
//SDADC DMA 循环缓冲布局 (双声道, samples=512 立体声对 per full DMA):
//   每个 L+R 立体声对 = 2×16bit = 4 bytes, buf_auxadc[512] = 2048 bytes = 512 pairs
//   flag=1 (Half Done):  pairs[0..255] 是新数据, pairs[256..511] 是上一次DMA的旧数据
//   flag=0 (Full Done):  pairs[256..511] 是新数据, pairs[0..255] 正被下一DMA周期覆盖
//   每个 sample 是 16-bit, 一对 L+R = 4 bytes
//
//输出到 DAC FIFO (AUBUFDATA = 32-bit 容器, 左 16 + 右 16):
//   每个 AUX 立体声对打包成 (right << 16) | left。

AT(.com_text.isr.sdadc)
void __wrap_auxadc_pcm_to_dac(u8 flag, u8 *adc_buf, u16 adc_samples)
{
    u16 *buf16 = (u16 *)adc_buf;
    u16 samples = adc_samples;

    // 关键: 退出 DAC FIFO reset 状态, 否则 FIFO 冻结不会消费
    AUBUFCON &= ~BIT(0);

    // ★ 修复噪音: DMA循环缓冲, flag=1时只读前半(安全区), flag=0时只读后半(安全区)
    //   DMA循环写入 pairs[0..samples-1] 个L+R立体声对:
    //   Half Done (flag=1): DMA刚写完前半[0..samples/2), 正在写后半[samples/2..samples)
    //     → 前半安全可读, 后半正在被DMA写入不能碰
    //   Done     (flag=0): DMA刚写完后半[samples/2..samples), 开始覆盖前半(下一DMA周期)
    //     → 后半安全可读, 前半正在被DMA覆盖不能碰
    //   原来的代码无视flag每次都写全部samples → 在半满是读到旧数据, 在全满是读到被覆盖数据 → 周期性噪音
    u16 start, end;
    if (flag == 1) {
        start = 0;
        end   = samples / 2;       // Half Done: 只读前半
    } else {
        start = samples / 2;
        end   = samples;            // Done: 只读后半
    }

    for (u16 i = start; i < end; i++) {
        if (AUBUFCON & BIT(8)) {
            //FIFO 已满, 跳过剩余
            break;
        }
        u16 left  = buf16[i * 2 + 0];
        u16 right = buf16[i * 2 + 1];
        AUBUFDATA = ((u32)right << 16) | (u32)left;
    }
}