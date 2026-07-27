# A3 任务教学：AUX → IIS Master SRCTX（零基础小白版）

> **适用对象**：已完成 A1（PCM→DAC）和 A2（AUX→DAC）任务的同学。
> **目标**：理解 IIS 协议基础，掌握 BT8920A2 上"IIS_MASTER_SRCTX"模式的实现与调试。
> **配套工程**：[`app/`](../../app/)；详细参考 [`SDK入门与任务实施指南.md`](SDK入门与任务实施指南.md) 第 9 节、[`docs/I2S驱动资料.md`](I2S驱动资料.md)。

---

## 第 0 节 · A3 任务在做什么？

**一句话**：把 AUX 输入的音频信号（电脑/手机播放的声音）同步推出两路：
1. **DAC 模拟输出**（耳机听到声音，复用 A2）
2. **IIS 数字输出**（开发板的 PE5/PE6/PE7 三个引脚同步输出 I²S 数字音频）

```text
   电脑音频 (1 kHz 正弦)
       ↓ AUX 接口 (PB1/PB2, A3 改的)
   SDADC 模拟通路 + 数字采样
       ↓ DMA 搬运 (512 samples = ~10.7 ms @ 48 kHz)
   buf_auxadc[512]
       ↓ DMA 半中断 / 全中断
   auxadc_isr()
       ↓ __wrap_auxadc_pcm_to_dac (A2 已重写)
   DAC FIFO
       ├─→ DAC 模拟输出 → 耳机     (A2 链路)
       └─→ DACDIGCON0 BIT(23)=1
              ↓ 走 DAC 内部 SRC 路径
            IIS_MASTER_SRCTX (本任务重写)
              ↓ BCLK / LRC / DO
            PE5 / PE6 / PE7 三个引脚
              ↓ 杜邦线 / 逻辑分析仪
            另一颗芯片 / 逻辑分析仪验证
```

---

## 第 1 节 · I²S（IIS）协议基础

### 1.1 I²S = Inter-IC Sound，是一种数字音频总线

I²S 用 **至少 3 根线** 传立体声音频：

| 信号 | 作用 | 方向 |
|---|---|---|
| **BCLK** (Bit Clock) | 数据移位时钟，每个 bit 一个脉冲 | Master 输出，Slave 输入 |
| **LRC** (Left-Right Clock, 也叫 WS) | 左右声道选择；`0` = 左，`1` = 右（或反之） | Master 输出，Slave 输入 |
| **DIN/DOUT** (Data In/Out) | 实际音频数据 | TX:Master→Slave，RX:Slave→Master |
| (可选) **MCLK** | 256fs / 128fs 系统时钟 | Master 输出 |

### 1.2 I²S Standard 时序

```text
              ┌───┐                                       ┌───┐
    LRC ──────┤   └───────────┐       ┌───────────────────┤   └──────  ...
              │ LEFT CHANNEL │ RIGHT │                   │
              └──────────────┘ CHANNEL                  

                       ┌─┐ ┌─┐ ┌─┐ ┌─┐                  ┌─┐ ┌─┐ ┌─┐ ┌─┐
    BCLK ───────────────┘ └─┘ └─┘ └─┘ └──────── ... ─────┘ └─┘ └─┘ └─┘ └─ ...

              MSB-first, 16-bit 一帧 (64 BCLK 一对 L+R)
              数据在 BCLK 上升沿采样 (具体看从机要求)
```

### 1.3 BT8920A2 的 I²S 模块

根据 [`app/bsp_ext/bsp_iis_ext.h`](../../app/bsp_ext/bsp_iis_ext.h)，芯片支持 10 种 IIS 模式，本任务用的是：

| 宏 | 含义 |
|---|---|
| `IIS_MASTER_SRCTX` | **主机**，数据从 **DAC SRC buffer** 直接推出（不占用 RAM DMA） |
| `IIS_MASTER_RAMTX` | 主机 + DMA 推 RAM 数据 |
| `IIS_MASTER_SRCTX_RAMRX` | 主机，发 DAC SRC + 收 RAM |
| `IIS_SLAVE_RAMRX` | 从机 + DMA 收 RAM 数据（**A4 任务**用这个） |

**SRCTX 的特点**：数据从 DAC 内部的 SRC（Sample Rate Converter）buffer 拿，所以**DAC 和 IIS 同步输出同一个音频**，声音一致。限制：
- **采样率必须 44.1 kHz 或 48 kHz**（这是 DAC 主时钟的硬件限制）
- **只有主机模式**支持 SRCTX

---

## 第 2 节 · 工程改动清单

A3 涉及 5 个文件，其中 3 个新增，2 个修改：

| 文件 | 改动 | 用途 |
|---|---|---|
| [`app/bsp_ext/bsp_iis_master_ext.c`](../../app/bsp_ext/bsp_iis_master_ext.c) | **新建** | `__wrap_iis_master_srctx_init()` 应用层实现 |
| [`app/bsp_ext/bsp_adc_aux_ext.h`](../../app/bsp_ext/bsp_adc_aux_ext.h) | **新建**（原本空） | A3 专用 `test_aux_adc2dac_for_a3()` 原型声明 |
| `docs/A3任务IIS_MASTER_SRCTX教学.md` | **新建** | 本文 |
| [`app/bsp_ext/bsp_adc_aux_ext.c`](../../app/bsp_ext/bsp_adc_aux_ext.c) | **修改** | 新增 `auxadc_param_init_for_a3()` + `test_aux_adc2dac_for_a3()` |
| [`app/projects/standard/main.c`](../../app/projects/standard/main.c) | **修改** | `case TEST_AUX_ADC2IISSRCTX:` 改调 `test_aux_adc2dac_for_a3()` + `iis_master_srctx_init()`。当前默认强制 `xcfg_cb.test_mode = TEST_AUX_ADC2DAC`（A2，引脚 PB1/PB2），切回 A3 测 IIS SRCTX 时需把强制模式改回 `TEST_AUX_ADC2IISSRCTX`。 |
| [`app/projects/standard/app.cbp`](../../app/projects/standard/app.cbp) | **修改** | Linker 加 `--wrap=iis_master_srctx_init`；Unit 增加 `bsp_iis_master_ext.c` |

---

## 第 3 节 · `--wrap` 链接器技巧

A3 沿用 A2 的 `--wrap=` 链接器覆盖法，让应用层的 `__wrap_iis_master_srctx_init()` 替换库里的 `iis_master_srctx_init()`。

```xml
<!-- app/projects/standard/app.cbp -->
<Linker>
    <Add option="--wrap=auxadc_pcm_to_dac" />     <!-- A2 -->
    <Add option="--wrap=iis_master_srctx_init" /> <!-- A3 (新增) -->
    ...
</Linker>
```

机制：
```text
库代码调用  iis_master_srctx_init()
            ↓ 链接器拦截
       __wrap_iis_master_srctx_init()    ← 我们写的实现
```

---

## 第 4 节 · 硬件 IO 接线（**重要**）

### 4.1 引脚约束

开发板可用引脚：**PB1, PB2, PE4, PE5, PE6, PE7**（PA3-PA7 未引出）。

### 4.2 引脚分配方案

AUX (A2 原本在 PE6/PE7) **必须改到 PB1/PB2**，给 IIS 让出 PE5/PE6/PE7：

| 引脚 | A3 用途 | A2 用途 |
|---|---|---|
| PB1 | **AUX-L 模拟输入** | (空闲) |
| PB2 | **AUX-R 模拟输入** | (空闲) |
| PE4 | 空闲 | 空闲 |
| PE5 | **IIS BCLK 输出** | (空闲) |
| PE6 | **IIS LRC 输出** | AUX-L2 输入 |
| PE7 | **IIS DO 输出** | AUX-R2 输入 |

**A2 ↔ A3 引脚切换**：
- A2 baseline 现在已统一走 PB1/PB2（避免每次切换拔线）
- A3 复用相同引脚（PB1/PB2 给 AUX，PE5/PE6/PE7 给 IIS）
- A4（RX）时把 AUX 杜邦线再拔掉（不需要）

### 4.3 MCLK 输出必须关闭

`IIS_G2` 映射的 MCLK 在 PB1。PB1 同时是 AUX-L 模拟输入。
- **MCLK 输出 = 数字方波驱动到模拟脚** → 冲突！
- 所以 PC3 配置 `mclk_out_en = IIS_MCLK_OUT_DIS`

---

## 第 5 节 · 关键代码解读

### 5.1 `test_aux_adc2dac_for_a3()` （AUX 端，A3 专用）

```c
void test_aux_adc2dac_for_a3(void) {
    dac_spr_set(SPR_48000);       //★ DAC 48 kHz (SRCTX 必须)
    dac_set_dvol(DIG_N0DB);
    dac_set_avol(53);             //保留 A2 已调好的音量
    auxadc_param_init_for_a3();   //★ PB1/PB2 + 48 kHz
    auxadc_digital_init();
    auxadc_analog_init();
}

void auxadc_param_init_for_a3(void) {
    memset(&auxadc_cb, 0, sizeof(auxadc_cb));
    auxadc_cb.buf = (u8 *)&buf_auxadc[0];
    auxadc_cb.channel  = CH_AUXL_PB1 | CH_AUXR_PB2;  //★ 0x22, 不是 A2 的 0x33
    auxadc_cb.sample_rate = SPR_48000;               //★ 48 kHz, 不是 A2 的 16 kHz
    auxadc_cb.samples  = 512;                        //保留 A2 调优
    auxadc_cb.gain     = (8 << 6) | 15;              //保留 A2 降噪调参
    auxadc_irq_init();
}
```

### 5.2 `__wrap_iis_master_srctx_init()`（IIS 端，PC4 完整实现）

```c
AT(.com_text.iis_ext)
void __wrap_iis_master_srctx_init(void) {
    iis_cfg_t cfg;

    // 1) GPIO pre-init: PE5/PE6/PE7 输出 + 高速驱动
    GPIOEDE |= BIT(5) | BIT(6) | BIT(7);
    GPIOEFEN |= BIT(5) | BIT(6) | BIT(7);
    GPIOEDIR &= ~(BIT(5) | BIT(6) | BIT(7));
    GPIOEDRV |= BIT(5) | BIT(6) | BIT(7);

    //    PB1 (MCLK 位置) 关闭功能映射 + 不上拉 + 不输出, 完全归 AUX-L 模拟
    GPIOBDE  |= BIT(1);
    GPIOBDIR |= BIT(1);
    GPIOBPU  &= ~BIT(1);
    GPIOBFEN &= ~BIT(1);   //★ 关键: 不让 PB1 走 IIS MCLK 功能

    // 2) 时钟门控
    CLKGAT1 |= BIT(4);
    CLKGAT0 |= BIT(12);

    // 3) 应用层 iis_cfg_init() 库函数 (包揽 IISCON0/IISBAUD/FUNCMCON2/CLKGAT)
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode        = IIS_MASTER_SRCTX;
    cfg.iomap       = IIS_G2;             //★ PE5/PE6/PE7
    cfg.bit_mode    = IIS_16BIT;
    cfg.data_mode   = IIS_DATA_NORMAL;
    cfg.mclk_sel    = IIS_MCLK_256FS;
    cfg.mclk_out_en = IIS_MCLK_OUT_DIS;   //★ 关闭 MCLK 输出
    cfg.dma_en      = 0;                  //★ SRCTX 不需要 RAM DMA
    iis_cfg_init(&cfg);

    // 4) 显式启动 (大部分库在 iis_cfg_init 末尾已启动, 保险起见再调一次)
    iis_start();

    // 5) 验证 SRCTX 是否使能
    printf("DACDIGCON0=0x%X (BIT(23)=%d, SRCTX %s)\n",
           DACDIGCON0, (DACDIGCON0 & BIT(23)) ? 1 : 0,
           (DACDIGCON0 & BIT(23)) ? "ENABLED" : "DISABLED");
}
```

### 5.3 关键发现：8920A2 sfr.h 不暴露 IIS 寄存器

`/app/platform/header/sfr.h` **没有** `IISCON0`/`IISBAUD`/`IISDMACNT` 这些寄存器（530X 模板里有）！

**解决方案**：调应用层 API [`iis_cfg_init()`](../../app/bsp_ext/bsp_iis_ext.h)——库内部封装了所有寄存器配置，包括 `DACDIGCON0 |= BIT(23)` SRCTX 启用位。

我们只用：
1. 手动配 GPIO（PE5/PE6/PE7 输出、PB1 关功能映射）
2. 手动开时钟门控
3. 配 `iis_cfg_t` 结构体后调 `iis_cfg_init(&cfg)`

---

## 第 6 节 · PC（增量测试）步骤

每个 PC 是一步可独立测试的最小改动。

### PC1：链接器 wiring

| 文件 | 改动 |
|---|---|
| `main.c:42` | `xcfg_cb.test_mode = TEST_AUX_ADC2IISSRCTX;` |
| `app.cbp` | `<Linker>` 加 `--wrap=iis_master_srctx_init`；`<Unit>` 加 `bsp_iis_master_ext.c` |
| `bsp_iis_master_ext.c`（新建） | 空 `__wrap_iis_master_srctx_init()` 打一行字符串 |

**验证**：串口打印 `--->__wrap_iis_master_srctx_init (PC1 stub)` → `--wrap` 链接生效。

### PC2：AUX 改 PB1/PB2 + DAC 48K

| 文件 | 改动 |
|---|---|
| `bsp_adc_aux_ext.h`（新建） | 声明 `test_aux_adc2dac_for_a3()` |
| `bsp_adc_aux_ext.c` | 新增 `auxadc_param_init_for_a3()`（channel=0x22, sample_rate=48K）+ `test_aux_adc2dac_for_a3()`（dac_spr_set SPR_48000）|
| `main.c:60` | `test_aux_adc2dac();` → `test_aux_adc2dac_for_a3();` |

**硬件**：拔下 AUX 线 PE6/PE7，改插 PB1/PB2。

**验证**：
- 串口打印 `auxadc_cb.channel = 0x22`
- AUX 1 kHz 输入，耳机听到 1 kHz

### PC3：IIS GPIO 初始化

| 文件 | 改动 |
|---|---|
| `bsp_iis_master_ext.c` | PE5/PE6/PE7 输出 + 高速驱动；PB1 关功能映射；开 CLKGAT |

**验证**：万用表 / 示波器测 PE5/PE6/PE7 都是低电平（idle），AUX 仍然工作。

### PC4：IIS SRCTX 启用

| 文件 | 改动 |
|---|---|
| `bsp_iis_master_ext.c` | 配 `iis_cfg_t` (mode=IIS_MASTER_SRCTX, iomap=IIS_G2, 16bit, 256fs, MCLK_DIS) → 调 `iis_cfg_init(&cfg)` + `iis_start()` |

**验证**：DACDIGCON0 BIT(23) = 1 (SRCTX 已启用)；逻辑分析仪抓 PE5/PE6/PE7 应该有波形。

### PC5：完整数据验证

**硬件**：LA1010 接线 CH0→PE5(BCLK), CH1→PE6(LRC), CH2→PE7(DO), GND→开发板 GND。

**LA1010 设置**：
- 10 MHz 采样率，100 K 深度
- 触发：CH1 (LRC) 上升沿
- I2S 解码器：16-bit, MSB first, normal mode (data right-shifted by 1)

**AUX 输入 1 kHz 正弦波**，重新抓：

**实测数据**（LA1010 解析结果）：
```
Ch1 (L): 0xFF13 → 0xFF6C → 0xFFCC → 0x002B → 0x0088 → 0x00E5 → 0x0131 → 0x018D
        → 0x01D8 → 0x021B → 0x0257 → 0x0288 → 0x02AB → 0x02C4 → 0x02BA → 0x02D2
Ch2 (R): 0xFF73 → 0xFFCF → 0x002B → 0x0084 → 0x00DC → 0x0131 → 0x0182 → 0x01D8
        → 0x01C9 → 0x020A → 0x0243 → 0x0270 → 0x0293 → 0x02AD → 0x02BA → 0x02D2
```

→ 这是个 **清晰的正弦波爬升段**（从 -237 到 +722，约 +959 跨越），符合 1 kHz 正弦在 48 kHz 采样率下 1/3 周期的特征 ✅

LRC 频率实测 = **47.85 kHz**（接近理论 48 kHz）✅

---

## 第 7 节 · 关键寄存器 / 库 API

| 项 | 值 / 说明 |
|---|---|
| `iis_cfg_init()` 入口 | [`app/bsp_ext/bsp_iis_ext.h:99`](../../app/bsp_ext/bsp_iis_ext.h#L99) |
| `IIS_MASTER_SRCTX` 枚举值 | `bsp_iis_ext.h:44` = `(IISCFG_MASTER \| IISCFG_TX \| IISCFG_SRC)` |
| `IIS_G2` IO 映射枚举 | `bsp_iis_ext.h:27`：MCLK=PB1, BCLK=PE5, LRC=PE6, DO=PE7, DI=PB2 |
| `IIS_MCLK_OUT_DIS` | `bsp_iis_ext.h:73`：关闭 MCLK 输出 |
| `DACDIGCON0 BIT(23)` | **SRCTX 输出使能位**（库内部配） |
| `CLKGAT1 BIT(4)` | **IIS 模块时钟门控** |
| `CLKGAT0 BIT(12)` | **IIS 时钟源门控** |

---

## 第 8 节 · 已知问题 / 后续调优

### 8.1 DAC 模拟输出噪声（PC3/PC4 期间用户反馈）

**现象**：
- AUX 1 kHz 输入 → DAC 耳机听到 1 kHz，但是**有很大的噪声**，示波器看波形**频率跳动 + 毛刺多**
- DAC FIFOCNT 在 A2/PC3 时一直 575 (满)；启用 SRCTX 后应该有动态变化

**下一步**（独立任务 / 待分配）：
1. 检查 PC2 时 avol=53 / gain=(8<<6)|15 是否仍适合 48 kHz
2. 试 `samples=256` 或 `samples=1024` 对比 FIFO 余量
3. 看 DAC PHASECOMP 是否需要动态调整（同 A2 教学中的 `aubuf_adjust()` 思路，但 SRCTX 模式可能不需要）
4. 试 44.1 kHz 而不是 48 kHz（取决于 PLL 噪声）

### 8.2 BCLK 频率偏差

**实测**：BCLK = 1.667 MHz；LRC = 47.85 kHz
**理论**：BCLK 应 = 1.536 MHz (48k × 32)
**偏差** ≈ 8.5%

**可能原因**：库 `iis_cfg_init()` 选用了不同的 mclk_sel（128fs 而不是 256fs）或不同 bdiv/mdiv 组合。在 `iis_cfg_t.mclk_sel = IIS_MCLK_256FS` 上改为 `IIS_MCLK_128FS` 看是否改变。

### 8.3 sfr.h 未暴露 IIS 寄存器

8920A2 SDK 的 sfr.h 不包含 `IISCON0`/`IISBAUD`。直接读/写 IIS 寄存器必须知道 SFR 地址。如果后续需要，**只能查芯片手册或通过反汇编 libdrivers.a**。

---

## 第 9 节 · 检查清单（A3 完成度确认）

- [x] AUX (PB1/PB2) → ADC → DMA → DAC FIFO 链路通
- [x] DAC FIFO → IIS SRCTX 链路通
- [x] IIS BCLK/LRC/DO 三路数字信号正确输出
- [x] I2S 协议解码数据 = AUX 输入音频
- [x] `--wrap=iis_master_srctx_init` 链接覆盖生效
- [x] A2 baseline 保留接口（`test_aux_adc2dac()`），但内部 channel 已改为 PB1/PB2（与 A3 保持一致，无需切换杜邦线）
- [ ] DAC 模拟输出噪声调优（PC3/PC4 期间仍有问题，待后续专项处理）

---

## 第 10 节 · A4 任务预告（占位）

A4 `iis_slave_ram_rx_2_dac()` 重写：

- `iis_cfg.mode = IIS_SLAVE_RAMRX`（从机、收数据到 RAM）
- `iis_cfg.iomap = IIS_G2`（BCLK=PE5 in, LRC=PE6 in, DI=PB2 in）
- 配置 DMA 双缓冲 + ISR 回调，把 RAM 数据搬到 DAC FIFO
- 用 `aubuf_adjust()` 根据 FIFO 余量调 `PHASECOMP`
- **不开 AUX**（直接接收另一颗芯片的 IIS TX）
- **PC6 计划**：写 docs/A4任务IIS_SLAVE_RAMRX教学.md，commit，**询问用户后** push

---

**提交**: `feat(a3): rewrite iis_master_srctx_init() via --wrap, DAC 48K + IIS_G2 SRCTX`
**分支**: `new_minimax_zgl01`
**完成时间**: 2026-07-27
**作者**: Claude / BT8920A2 learner

---

**A3 ✅ 完成。** IIS 数据内容验证通过；DAC 模拟输出降噪专项留在 A3.1（待用户通知后开启）。
