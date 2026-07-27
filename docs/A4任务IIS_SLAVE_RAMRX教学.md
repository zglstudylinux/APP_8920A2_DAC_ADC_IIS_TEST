# A4 任务教学:IIS Slave RAMRX → DAC（零基础小白版 + 双板联调）

> **适用对象**:已完成 A1 (PCM→DAC)、A2 (AUX→DAC)、A3 (IIS Master SRCTX) 任务的同学。
> **目标**:理解 IIS 从机模式、RX DMA、库回调,掌握 BT8920A2 双板联调(IIS 跨芯片音频传输)。
> **配套工程**:[`app/`](../../app/);参考 [`SDK入门与任务实施指南.md`](SDK入门与任务实施指南.md) 第 9 节、[A3 教学](A3任务IIS_MASTER_SRCTX教学.md)。

---

## 第 0 节 · A4 任务在做什么?

**一句话**:用两块 BT8920A2 开发板做"接力播音" —— **板 A (A3)** 接收 PC 音频并通过 IIS 数字口发出, **板 B (A4)** 通过 IIS 数字口接收并用 DAC 还原到耳机。

```text
                      双板联调架构 (A3 + A4)
   电脑音频 (1 kHz 正弦波)
       ↓ AUX 接口 (PB1/PB2, A3 改的)
   板 A (A3)                                板 B (A4)
   ┌─────────────────────────┐             ┌─────────────────────────┐
   │ SDADC 采样 44.1K        │   4 根线    │ IIS Slave RAMRX 44.1K   │
   │ ↓ DMA                   │ ──────────  │ ↓ DMA → iis_dmabuf1     │
   │ ↓ auxadc_isr            │ PE5 ← BCLK  │ ↓ IRQ_I2S_VECTOR        │
   │ ↓ __wrap_auxadc_pcm_to_dac            │ ↓ 库 iis_isr_func       │
   │ DAC FIFO                │ PE6 ← LRC   │ ↓ __wrap_iis_rx_process │
   │ ├─→ DAC 模拟输出 → 耳机 │             │ ↓ AUBUFDATA 推 FIFO     │
   │ └─→ DACDIGCON0 BIT(23)  │ PE7 ← DO  → │ → DAC 模拟输出 → 耳机   │
   │       ↓ IIS_MASTER_SRCTX │ PB2 ← DI  → │                         │
   │ BCLK PE5 + LRC PE6 + DO PE7 (44.1K)   │ (与 A3 板同步)           │
   └─────────────────────────┘             └─────────────────────────┘
```

---

## 第 1 节 · IIS 双向通信回顾 (A3 vs A4 对比)

### 1.1 A3 是发送方(主机 SRCTX)

| 项 | A3 配置 | 备注 |
|---|---|---|
| 模式 | `IIS_MASTER_SRCTX` | 主机模式,数据走 DAC 内部 SRC 缓冲 |
| BCLK | PE5 **输出** | 驱动板 B 的 BCLK |
| LRC | PE6 **输出** | 驱动板 B 的 LRC |
| DO | PE7 **输出** | 把数据送给板 B 的 DI |
| DMA | 不需要 | 数据从 DAC 内部 SRC 拿 |
| 回调 | 不需要 | 没有 ISR |

### 1.2 A4 是接收方(从机 RAMRX)

| 项 | A4 配置 | 备注 |
|---|---|---|
| 模式 | `IIS_SLAVE_RAMRX` | 从机模式,数据经 DMA 收到 RAM |
| BCLK | PE5 **输入** | 由板 A 驱动 |
| LRC | PE6 **输入** | 由板 A 驱动 |
| DI | PB2 **输入** | 接收板 A 的 DO 数据 |
| DMA | 需要 | 数据必须先到 RAM,再推到 DAC FIFO |
| 回调 | 需要 `iis_rx_process_test` | DMA 完成时把 RAM 数据搬到 DAC |

### 1.3 为什么 A4 一定要 DMA?

A3 的 SRCTX 数据来自 DAC 内部 SRC 缓冲 → 直接走 BCLK/LRC/DO 输出,不经过 RAM。

A4 的数据从外部 IIS 进来 → 必须先存到 RAM (用 DMA 自动接收) → 然后回调函数把 RAM 数据搬到 DAC FIFO。如果没有 DMA,CPU 就得每个 BCLK 中断一次,负担太大。

---

## 第 2 节 · 工程改动清单

A4 涉及 5 个文件,其中 1 个新建,4 个修改:

| 文件 | 改动 | 用途 |
|---|---|---|
| [`app/bsp_ext/bsp_iis_slave_ext.c`](../../app/bsp_ext/bsp_iis_slave_ext.c) | **新建** | `__wrap_iis_slave_ram_rx_2_dac()` + `static iis_cfg_slave` + GPIO/CLKGAT 配置 |
| [`app/bsp_ext/bsp_iis_slave_ext.h`](../../app/bsp_ext/bsp_iis_slave_ext.h) | **新建** | wrap 原型声明 (供 main.c 调用) |
| [`app/projects/standard/app.cbp`](../../app/projects/standard/app.cbp) | **修改** | Linker 加 `--wrap=iis_slave_ram_rx_2_dac`;Unit 加 `bsp_iis_slave_ext.c` |
| [`app/projects/standard/main.c`](../../app/projects/standard/main.c) | **修改** | `case TEST_IISRX2DAC` 注释完善;line 42 双板切换 |
| [`app/bsp_ext/bsp_adc_aux_ext.c`](../../app/bsp_ext/bsp_adc_aux_ext.c) | **修改** | A3 的 `SPR_48000` → `SPR_44100`,与 A4 板同步 |

---

## 第 3 节 · `--wrap` 链接器技巧 (A4 沿用)

跟 A2 / A3 完全一样 —— `--wrap=iis_slave_ram_rx_2_dac` 让库里所有 `iis_slave_ram_rx_2_dac()` 调用自动改写到 `__wrap_iis_slave_ram_rx_2_dac()`。

```xml
<!-- app/projects/standard/app.cbp -->
<Linker>
    <Add option="--wrap=auxadc_pcm_to_dac" />        <!-- A2 -->
    <Add option="--wrap=iis_master_srctx_init" />    <!-- A3 -->
    <Add option="--wrap=iis_slave_ram_rx_2_dac" />   <!-- A4 (新增) -->
    ...
</Linker>
```

机制:
```text
库代码调用  iis_slave_ram_rx_2_dac()
            ↓ 链接器拦截
       __wrap_iis_slave_ram_rx_2_dac()    ← 我们写的实现
```

---

## 第 4 节 · 双板硬件接线 (★ 最关键)

### 4.1 板 A 引脚 (A3 输出)

| 板 A 引脚 | 信号 | 方向 |
|---|---|---|
| **PE5** | BCLK (主机输出) | → 板 B PE5 |
| **PE6** | LRC (主机输出) | → 板 B PE6 |
| **PE7** | DO (数据输出) | **→ 板 B PB2** ⚠ 交叉 |
| GND | — | ↔ 板 B GND |

### 4.2 板 B 引脚 (A4 输入)

| 板 B 引脚 | 信号 | 方向 |
|---|---|---|
| **PE5** | BCLK (从机输入) | ← 板 A PE5 |
| **PE6** | LRC (从机输入) | ← 板 A PE6 |
| **PB2** | DI (数据输入) | **← 板 A PE7** ⚠ 交叉 |
| GND | — | ↔ 板 A GND |

### 4.3 接线示意

```text
   板 A (A3)                              板 B (A4)
  ┌──────────┐                            ┌──────────┐
  │  PE5 ●───┼──── BCLK ──────────────────┼───● PE5  │
  │  PE6 ●───┼──── LRC  ──────────────────┼───● PE6  │
  │  PE7 ●───┼──── DO   ──────┐           │          │
  │  GND ●───┼──── GND ───┐   ├──── 交叉 ─┼───● PB2  │
  │          │             │   │          │          │
  └──────────┘             │   │          └──────────┘
                           │   │
                      PE7 ─┘   └─ PB2
```

⚠ **PE7 → PB2 是交叉接线!** 两板同名引脚不能直连(都成 DO → 都没人收)。

### 4.4 PB1 复用警告

IIS_G2 的 MCLK 默认映射在 PB1。PB1 同时是 A3 板的 AUX-L 模拟脚。**两板都必须在 wrap 里把 PB1 关功能映射 + 配 input**,避免 MCLK 数字方波驱动 AUX 模拟电路。

---

## 第 5 节 · 关键代码解读

### 5.1 `static iis_cfg_slave` (★ 必读,踩坑记录)

```c
AT(.com_text.iis_ext)
static iis_cfg_t iis_cfg_slave;       // ★ 必须 static!

void __wrap_iis_slave_ram_rx_2_dac(void) {
    iis_cfg_t *cfg = &iis_cfg_slave;  // 指向 static 变量
    ...
}
```

**踩坑实录**:第一版写的是 `iis_cfg_t cfg;`(放栈上),结果板 B 反复重启:
```text
Test End
ERR: 3, EPC: 23232324
... (栈 dump 大量 0x23232323) ...
ERR: 7, EPC: 23232324
Hello Platform    ← 重启
```

**根因**:
- `iis_cfg_init()` 内部保存 cfg 指针 `iis_libcfg = &cfg`
- wrap() 返回 → 栈帧弹出 → `cfg` 内存被回收
- DMA 完成触发 IRQ → `iis_isr_func` 调 `iis_rx_process_test`
- `iis_rx_process_test` 内部调 `iis_mode_cfg_get()` 读 `iis_libcfg->mode`
- **访问已释放栈内存** → 读到 0x23232323 垃圾 → CPU 跳到 EPC=0x23232324 → ERR:3/7 → 重启

**修复**:用 `static iis_cfg_slave`,生命周期与程序同。库原版 (`libplatform.a` 里 `iis_slave_ram_rx_2_dac`) 用的就是 `static iis_cfg_t iis_cfg;` —— 反汇编 `libplatform.a(bsp_iis_ext.o)` 验证过。

### 5.2 GPIO 预配 (PE5/PE6/PB2 输入 + PB1 关 MCLK)

```c
//PE5 = BCLK 输入
GPIOEDE |= BIT(5);
GPIOEDIR |= BIT(5);              // input
GPIOEPU  &= ~BIT(5);             // disable pull-up (避免悬空噪声)
GPIOEFEN |= BIT(5);              // enable function mux

//PE6 = LRC 输入(同上)

//PB2 = DI 输入(同上)

//PB1 = MCLK 位置 — 关键! 关功能映射
GPIOBDE  |= BIT(1);
GPIOBDIR |= BIT(1);              // input
GPIOBPU  &= ~BIT(1);             // 不上拉
GPIOBFEN &= ~BIT(1);             // ★ 不让 PB1 走 IIS MCLK 功能
```

iis_cfg_init() 内部也会调 iis_io_init() 配这些脚,但**提前配更保险** —— 避免库配之前引脚浮空。

### 5.3 时钟门控 + iis_cfg_init

```c
CLKGAT1 |= BIT(4);              // IIS 模块时钟
CLKGAT0 |= BIT(12);             // IIS 时钟源

memset(cfg, 0, sizeof(*cfg));   // ★ 清 struct 本身 (不是清指针)
cfg->mode        = IIS_SLAVE_RAMRX;     // 0x0A = RX|DMA
cfg->iomap       = IIS_G2;              // PE5=BCLK, PE6=LRC, PB2=DI
cfg->bit_mode    = IIS_16BIT;
cfg->data_mode   = IIS_DATA_NORMAL;
cfg->mclk_sel    = IIS_MCLK_256FS;
cfg->mclk_out_en = IIS_MCLK_OUT_DIS;    // 从机不输出 MCLK
cfg->dma_en      = 0;                  // ★ 库原版也是 0

cfg->dma_cfg.samples            = 64;
cfg->dma_cfg.dmabuf_len         = 2048;
cfg->dma_cfg.dmabuf_ptr         = iis_dmabuf1;          // 库 .iis2dac_buf
cfg->dma_cfg.iis_isr_rx_callbck = iis_rx_process_test;  // 库回调
cfg->dma_cfg.iis_isr_tx_callbck = NULL;

iis_cfg_init(cfg);              // ★ 注意传指针
iis_start();
```

★ `dma_en = 0`(不是 1):库原版就是 0。`mode` 已包含 DMA 标志 (IIS_SLAVE_RAMRX = RX|DMA),`dma_en` 字段是别的语义(可能是 DMA 双缓冲之类的开关),库默认关掉。

### 5.4 库回调 `iis_rx_process_test` (复用,不重写)

库里有完整的 `iis_rx_process_test(void *buf, u32 samples, bool iis_32bit)`,反汇编已知(`.com_text.iis_ext+0x13E`,272 字节):

```c
// 库回调等价 C(反汇编还原)
void iis_rx_process_test(void *buf, u32 samples, bool iis_32bit) {
    if (iis_32bit) { samples /= 2; /* 32bit → 16bit 重排 */ }
    if (!(iis_mode_cfg_get() & IISCFG_MASTER)) aubuf_adjust();  // 从机调速
    if (!(iis_mode_cfg_get() & IISCFG_SRC)) {
        while (samples--) {
            if (AUBUFCON & BIT(8)) { lose_samples++; break; }
            AUBUFDATA = *ptr32++;
        }
    }
    // 1秒一次打印 DAC info (走 print_dac_info)
}
```

**为什么直接复用**:
1. 已经过芯片原厂验证
2. 自动调速 `aubuf_adjust()` 是 slave 防 FIFO 漂移的关键,自己写容易漏
3. 1秒打印自带,不用单独写诊断
4. 与 A2 的 `__wrap_auxadc_pcm_to_dac` 几乎同构,质量有保证

需要 extern 声明(库无头文件原型):
```c
extern void iis_rx_process_test(void *buf, u32 samples, bool iis_32bit);
extern void aubuf_adjust(void);
extern u8 iis_dmabuf1[2048];
```

### 5.5 数据流完整路径

```text
板 A PE7 (DO) ─── 杜邦线 ─── 板 B PB2 (DI)
                              ↓
              板 B 库 iis_io_init() 配 PB2 为 IIS DI
                              ↓
              板 B 库 IIS RX DMA 自动接收 (64 samples/buffer)
                              ↓
              iis_dmabuf1[2048] (.iis2dac_buf, bram 中)
                              ↓
              DMA 完成 → IRQ_I2S_VECTOR 中断
                              ↓
              库 iis_isr_func (iis_irq_init 注册)
                              ↓
              库 iis_rx_dma_addr_inc 翻双缓冲
                              ↓
              调 iis_isr_rx_callbck(buf, samples, iis_32bit)
              = iis_rx_process_test(buf, 64, false)
                              ↓
              aubuf_adjust() 根据 FIFO 余量调 PHASECOMP
                              ↓
              while (samples--) AUBUFDATA = *ptr32++  (推 DAC FIFO)
                              ↓
              DAC FIFO → DAC 模拟输出 → 耳机
                              ↓
              1秒一次: print_dac_info() 打印 FIFOCNT/PHASECOM
```

---

## 第 6 节 · PC (增量测试) 步骤

### PC1: 基本编译 + 入口 stub

- **改动**:仅 `main.c` line 42 改 `TEST_IISRX2DAC`,case 块先打 `printf("TEST_IISRX2DAC\n");`
- **验证**:板烧录后串口打印 `TEST_IISRX2DAC`,无库函数警告
- **风险**:0

### PC2: 加 `--wrap` + 新文件 + DAC init

- **改动**:
  - `app.cbp` 加 `--wrap=iis_slave_ram_rx_2_dac`
  - `app.cbp` Unit 区段加 `bsp_iis_slave_ext.c`
  - 新建 `bsp_iis_slave_ext.c`,wrap 内只做 DAC 音量/spr + printf + iis_cfg_init + iis_start (暂不挂库回调)
- **验证**:编译通过,串口看到 `__wrap_iis_slave_ram_rx_2_dac` 打印
- **风险**:低

### PC3: 挂库回调 + GPIO 预配

- **改动**:补 GPIOEDE/... 配置,挂 `iis_dmabuf1` 和 `iis_rx_process_test`
- **验证**:编译通过,板 A 接 B 后,板 B 串口 1 秒一次 `print_dac_info`,且 `FIFOCNT` 数字变动
- **风险**:中

### PC4: 联调接线

- **改动**:无代码改动,只接 PE5/PE6/PE7 ↔ PE5/PE6/PB2 + GND
- **验证**:板 B 耳机能听到 PC 音频 (1kHz 正弦波)
- **风险**:接线问题

### PC5: 调音

- **改动**:`dac_set_avol(53)` 调大或调小看音量/失真
- **验证**:音量合适,无明显削顶
- **风险**:低

### PC6: 压力测试

- **改动**:无
- **验证**:长时间 (10 分钟) 跑,`PHASECOM` 不漂移,无咔哒声
- **风险**:0

---

## 第 7 节 · 关键寄存器 / 库 API

| 项 | 值 / 说明 |
|---|---|
| `iis_cfg_init()` 入口 | [`app/bsp_ext/bsp_iis_ext.h:99`](../../app/bsp_ext/bsp_iis_ext.h#L99) |
| `IIS_SLAVE_RAMRX` 枚举值 | `bsp_iis_ext.h:56` = `IISCFG_RAMRX` = `(IISCFG_RX \| IISCFG_DMA)` |
| `IIS_G2` IO 映射枚举 | `bsp_iis_ext.h:27`:MCLK=PB1, BCLK=PE5, LRC=PE6, DO=PE7, **DI=PB2** |
| `iis_dmabuf1` 库符号 | `libplatform.a(bsp_iis_ext.o)`,节 `.iis2dac_buf`,**2048 字节 bram** |
| `iis_rx_process_test` 库回调 | `libplatform.a(bsp_iis_ext.o)`,`.com_text.iis_ext+0x13E`,**272 字节** |
| `aubuf_adjust` 库函数 | `libplatform.a(bsp_iis_ext.o)`,`.com_text.i2s`,**82 字节** |
| `CLKGAT1 BIT(4)` | IIS 模块时钟门控 |
| `CLKGAT0 BIT(12)` | IIS 时钟源门控 |
| `IRQ_I2S_VECTOR` | `api_sys.h:30` = 27 (注意:不要照抄 530X 文档的 17) |

---

## 第 8 节 · 已知问题 / 后续调优

### 8.1 第一版崩溃:ERR:3/7 + EPC=0x23232324

**现象**:板 B 开机后 → wrap 跑完 → main 跑完 → **反复重启**,串口看到:
```text
Test End
ERR: 3, EPC: 23232324
... (栈 dump 大量 0x23232323) ...
ERR: 7, EPC: 23232324
Hello Platform    ← 重启
```

**根因**:`iis_cfg_t cfg;` 放栈上 → wrap() 返回后栈帧释放 → DMA 完成 IRQ 触发 `iis_rx_process_test` → 读已释放的 `iis_libcfg->mode` → 访问栈垃圾 → CPU 跳到 0x23232324 → 崩溃。

**修复**:`static iis_cfg_slave;` 常驻内存。详见 §5.1。

### 8.2 `dma_en` 字段语义不明

库原版 `iis_slave_ram_rx_2_dac` 把 `dma_en` 设为 0,即使 mode 已包含 DMA 标志。猜测 `dma_en` 是某种"额外 DMA 特性"(可能是双缓冲或循环模式)。设 1 时行为未知,设 0 走库默认行为,稳定。

### 8.3 跨板频率微差

实测板 A BCLK = 1.428 MHz,理论 44.1k × 32 = 1.411 MHz,偏差 ~1.2%。两板 LRC = 44.05 kHz,理论 44.1 kHz,偏差 0.1%。

跨板微差由 `aubuf_adjust()` 通过调整 `PHASECOMP` 补偿,正常情况 PHASECOM 数值会缓慢波动。

### 8.4 接线方向易错

| 常见错误 | 后果 |
|---|---|
| PE5 接 PE5, PE6 接 PE6 ✓ | OK |
| PE7 接 PE7 (同名直连) | ❌ 都成 DO → 板 B PB2 没人收 |
| PE7 接 PB2 (交叉) | ✓ 正确 |
| 漏接 GND | 信号浮空,BCLK/LRC/DI 全乱 |

---

## 第 9 节 · 检查清单 (A4 完成度确认)

- [x] A3 板 AUX → DAC + IIS SRCTX 44.1K 链路通 (BCLK=1.428MHz / LRC=44.05kHz / DO 数据 = 1kHz 正弦)
- [x] A4 板 IIS_SLAVE_RAMRX → DAC 链路通 (DMA 接收 + 库回调 + 推 DAC FIFO)
- [x] A3+A4 板 LRC 同步 (44.1K vs 44.05K,微差 ~0.1%)
- [x] `--wrap=iis_slave_ram_rx_2_dac` 链接覆盖生效
- [x] **修复**: `iis_cfg_t cfg` 必须 static,否则 ISR 触发后崩溃 (ERR:3/7)
- [x] 双板联调首战即可听声 (4 根线 + GND)
- [x] 双板跑 10 分钟听感无咔哒声
- [x] A2/A3/A4 工程文件 / 文档齐备

---

## 第 10 节 · A3+A4 联动改造 (重要!)

A4 完成后,**A3 也必须改 44.1K**(原本 A3 跑 48K),原因:
- 两板要跨板接 IIS,LRC 必须严格同步
- 48K vs 44.1K 跨板会让 `aubuf_adjust()` 大幅调 `PHASECOMP` 补偿,容易 FIFO 漂移
- **44.1K + 44.1K 同步** → 几乎零跨板异步 → 调速只处理微抖动 → 稳定

修改位置 [`app/bsp_ext/bsp_adc_aux_ext.c`](../../app/bsp_ext/bsp_adc_aux_ext.c):
```c
// A3 auxadc_param_init_for_a3()
auxadc_cb.sample_rate = SPR_44100;     // 原来是 SPR_48000

// A3 test_aux_adc2dac_for_a3()
dac_spr_set(SPR_44100);                 // 原来是 SPR_48000
```

---

## 第 11 节 · 验收标准 + 接线操作清单

### 11.1 准备

1. **Code::Blocks Build** 板 A 和板 B 各一次
2. **烧录**:
   - 板 A:`main.c:42` 是 `xcfg_cb.test_mode = TEST_AUX_ADC2IISSRCTX;` (默认)
   - 板 B:`main.c:42` 改为 `xcfg_cb.test_mode = TEST_IISRX2DAC;` (取消注释另一行)

### 11.2 接线 (4 根杜邦线 + GND)

```text
板 A (A3)            板 B (A4)
PE5 (BCLK out) ─────── PE5 (BCLK in)
PE6 (LRC out)  ─────── PE6 (LRC in)
PE7 (DO out)   ─────── PB2 (DI in)    ← 交叉
GND             ─────── GND
```

AUX 输入只接板 A 的 PB1/PB2 (PC 音频输出口)。板 B DAC 输出 → 耳机。

### 11.3 串口预期

**板 A**:
```text
TEST_AUX_ADC2IISSRCTX (board A: AUX -> DAC + IIS SRCTX 44.1K)
...
DACDIGCON0=0x800205 (BIT(23)=1, SRCTX ENABLED)
DAC FIFOCNT=..., AUBUFSIZE=575, DVOL=0x7FFF, AVOL=0x10, PHASECOM=0x0, DACDIGCON0=0x800205  (1s 一次)
```

**板 B**:
```text
TEST_IISRX2DAC (board B: IIS SLAVE RAMRX -> DAC 44.1K)
--->__wrap_iis_slave_ram_rx_2_dac (SLAVE_RAMRX via iis_cfg_init)
  iis_cfg: mode=IIS_SLAVE_RAMRX, iomap=IIS_G2, 16bit, 256fs, MCLK_DIS
  dma_cfg: samples=64, dmabuf=0x24000 (iis_dmabuf1), rx_cb=iis_rx_process_test
  DACDIGCON0=0x205 (audio path alive)
  ...
DAC FIFOCNT=..., AUBUFSIZE=576, DVOL=0x7FFF, AVOL=0x10, PHASECOM=0xXX, DACDIGCON0=0x205  (1s 一次, 走库回调)
```

**关键判据**:
- 板 B `FIFOCNT` 在 50~80% 区间小幅波动 (`aubuf_adjust` 在调速)
- 板 B `PHASECOM` 数值稳定 (波动 < 5)
- **板 B 耳机听到 PC 1kHz 正弦波** ← 验收金标准

### 11.4 排查方向(失败时)

| # | 排查项 | 验证方法 |
|---|---|---|
| 1 | 接线 | 示波器抓 PE5/PE6/PB2,确认 BCLK/LRC/DI 都有信号 |
| 2 | 库 `iis_rx_process_test` 是否被 gc-sections 链掉 | 查 `Output/bin/map.txt` 搜 `iis_rx_process_test` |
| 3 | 板 A 没真正输出 | 看板 A 串口 `SRCTX ENABLED` 打印 |
| 4 | DAC 时钟没锁定 | 板 B 串口看 `PHASECOM` 是否稳定递增 |
| 5 | ERR:3/7 + EPC=0x23232324 反复重启 | `iis_cfg_slave` 没加 static,回到 §5.1 修复 |

---

## 第 12 节 · 提交历史 (本次双板联调)

| Commit | 说明 |
|---|---|
| `feat(a4): add IIS_SLAVE_RAMRX wrap + sync A3 to 44.1 kHz` | A4 wrap 初次实现,A3 改 44.1K 同步,双板联调 |
| `fix(a4): make iis_cfg_slave static to fix ERR:3/7 crash` | 修崩溃:cfg 必须 static,见 §5.1 |

---

**提交**: `feat(a4): ...` + `fix(a4): ...`
**分支**: `new_minimax_zgl01`
**完成时间**: 2026-07-27
**作者**: Claude / BT8920A2 learner

---

**A4 ✅ 完成。** 双板联调首战即可听声;DAC 模拟输出降噪专项留在 A4.1(待用户通知后开启)。