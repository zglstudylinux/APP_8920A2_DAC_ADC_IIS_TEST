APP_8920A2_DAC_ADC_IIS_NEW_TEST

> 面向 **BT8920A2 RISC-V 音频 SoC** 的 Code::Blocks 工程，主要用于：
>
> - 厂商 SDK（[app/platform](../app/platform)）的初学者上手；
> - 任务 A/B/C/D 的应用层实现（[docs/待完成任务.md](docs/待完成任务.md)）；
> - 验证 DAC / SDADC / IIS 通路在真实开发板上的工作行为。

**当前进度**：✅ A1 + A2 完成；⏳ A3 / A4 / B / C / D 待开始。
详见 [docs/待完成任务.md](docs/待完成任务.md)。

---

## 仓库结构

```text
.
├─ app/
│  ├─ bsp_ext/                       应用层扩展 BSP：重写库函数的位置
│  │  ├─ bsp_dac_ext.c / .h            DAC 配置（avol/dvol/spr_set/obuf_init）
│  │  ├─ bsp_adc_aux_ext.c / .h        AUX SDADC（PB1/PB2 通道，A2/A3 共用）
│  │  ├─ bsp_adc_pcm_to_dac_ext.c      应用层 __wrap_auxadc_pcm_to_dac（A2 任务）
│  │  ├─ bsp_iis_ext.h                 IIS 类型定义（A3/A4 待写实现）
│  │  └─ audio_sfr.h                   音频模拟寄存器宏
│  ├─ platform/                       厂商 SDK（不要改）
│  │  ├─ header/                       类型、宏、SFR、配置定义
│  │  ├─ libs/                          API 头 + 静态库（libplatform/libbtstack/...）
│  │  └─ bsp/                           SDK 基础 BSP（bsp_sys.c 是 A1 测试入口）
│  └─ projects/standard/              Code::Blocks 工程
│     ├─ main.c                         入口与 4 种测试模式分派
│     ├─ config.h / .c                  编译期配置
│     ├─ xcfg.h                          配置工具生成的运行时配置
│     ├─ ram.ld                          链接脚本
│     ├─ app.cbp                         Code::Blocks 工程（含 `--wrap=` 链接器选项）
│     └─ Output/bin/                     Build 产物（已 .gitignore）
├─ docs/                              文档
│  ├─ I2S驱动资料.md                    530X IIS 示例驱动（A3/A4 参考）
│  ├─ 待完成任务.md                     任务原文 + 当前进度总览（新版）
│  ├─ SDK入门与任务实施指南.md          工程文档（PC1~PC12 详细验证记录）
│  ├─ BT8920A2音频实验教学.md           零基础小白教学（A1 完整）
│  ├─ A2任务降噪调优教学.md             零基础小白教学（A2 完整 + PC7~PC12）
│  └─ reference/
│     └─ sine_16k_500hz.wav             A1 参考音频
└─ README.md                          你正在读的
```

---

## 工具链

- **IDE**：Code::Blocks（工程已包含 `.cbp`）；
- **编译器**：厂商 `riscv32`（RISC-V 32-bit，`-march=rv32imacxbs1 -Os`）；
- **下载器**：厂商 `BT8920A2` Downloader（烧录 `Output/bin/app.dcf`）。

---

## 入门顺序（新终端推荐）

1. **读 [docs/待完成任务.md](docs/待完成任务.md)** 立刻知道哪些任务已完成、哪些没完成；
2. 阅读 [docs/SDK入门与任务实施指南.md](docs/SDK入门与任务实施指南.md) 第 0~7 章（基础概念 + 工具链）；
3. 在 Code::Blocks 打开 `app/projects/standard/app.cbp`，Build + 烧录；
4. 验证 `TEST_PCM2DAC` 模式输出 1 kHz 正弦波（基线，详见第 21 章）；
5. 按"入门顺序"再读 [docs/BT8920A2音频实验教学.md](docs/BT8920A2音频实验教学.md)（A1 完整小白教学）；
6. 如果做 A2，先读 [docs/A2任务降噪调优教学.md](docs/A2任务降噪调优教学.md)；
7. A3/A4 待开始时读 [docs/I2S驱动资料.md](docs/I2S驱动资料.md)（530X 示例，需注意与 8920A2 寄存器差异）。

---

## 任务状态

| 任务 | 状态 | 提交/文档 |
|---|---|---|
| 工程基线（PC1：Code::Blocks Build + 烧录 + 1 kHz 声音） | ✅ | [PC1](docs/SDK入门与任务实施指南.md#21-第一次运行日志逐行分析) |
| 任务 A1：DAC 输出 500/1k/2k Hz 三种正弦波 | ✅ | [PC2~PC6](docs/SDK入门与任务实施指南.md#22-任务-a1-第-1-步验证采样率-8k--16k) + [小白教学](docs/BT8920A2音频实验教学.md) |
| 任务 A2：AUX ADC → DAC（重写 `auxadc_pcm_to_dac`） | ✅ | [PC7~PC12](docs/SDK入门与任务实施指南.md#25-任务-a2-验证aux-adc--dacpc7) + [降噪调优](docs/A2任务降噪调优教学.md) |
| 任务 A3：IIS Master SRCTX（重写 `iis_master_srctx_init`） | ✅ | 分支 `new_minimax_zgl01` + [A3 教学](docs/A3任务IIS_MASTER_SRCTX教学.md)。AUX 杜邦线改 PB1/PB2，IIS_G2 (PE5/PE6/PE7)，DAC 48 kHz。逻辑分析仪验证 BCLK=1.667MHz / LRC=47.85kHz / DO 数据 = 1 kHz 正弦。**DAC 模拟输出降噪待 A3.1 专项**；当前默认测试模式已切回 `TEST_AUX_ADC2DAC`（A2，引脚 PB1/PB2）。 |
| 任务 A4：IIS Slave RAMRX → DAC（重写 `iis_slave_ram_rx_2_dac`） | ⏳ | 待开始（依赖 A3）|
| 任务 B：SDDAC 实战 | ⏳ | 待开始 |
| 任务 C：SDADC 实战 | ⏳ | 待开始 |
| 任务 D：IIS 端到端链路 | ⏳ | 待开始（依赖 A3+A4）|

---

## 关键代码改动一览

| 文件 | 作用 |
|---|---|
| `app/projects/standard/app.cbp` | 加 `--wrap=auxadc_pcm_to_dac` 链接器选项 |
| `app/projects/standard/main.c` | 强制 `xcfg_cb.test_mode = TEST_AUX_ADC2DAC`（A2 模式，引脚 PB1/PB2）|
| `app/platform/bsp/bsp_sys.c` | A1 测试入口 + 三张正弦表（500/1k/2k）+ `A1_CUR_FREQ` 宏 |
| `app/bsp_ext/bsp_dac_ext.c` | DAC 配置 + `dac_obuf_init()` 加 `AUBUFCON &= ~BIT(0)` |
| `app/bsp_ext/bsp_adc_aux_ext.c` | AUX 通道改 PB1/PB2 + 放开 PB1/PB2 路径 + samples=512 |
| `app/bsp_ext/bsp_adc_pcm_to_dac_ext.c` | `__wrap_auxadc_pcm_to_dac` 应用层实现 |
| `app/bsp_ext/bsp_iis_ext.h` | IIS 类型定义（A3/A4 实现已用 `iis_cfg_init()` 库 API）|
| `app/bsp_ext/bsp_iis_master_ext.c` | A3：`__wrap_iis_master_srctx_init()`（用 `iis_cfg_init()` 配 IIS_MASTER_SRCTX + IIS_G2 + MCLK_DIS）|
| `app/bsp_ext/bsp_adc_aux_ext.h` | A3：`test_aux_adc2dac_for_a3()` / `auxadc_param_init_for_a3()` 原型 |

---

## 仓库地址

```text
git@github.com:zglstudylinux:APP_8920A2_DAC_ADC_IIS_NEW_TEST.git
```

---

## 维护约定

- 每次完成一个子任务：先改代码 → Build → 烧录验证 → 把日志、示波器/Audacity 记录与说明追加到 `docs/SDK入门与任务实施指南.md` 或新文档 → `git commit` → `git push`。
- 不要提交 `app/projects/standard/Output/` 下的生成产物（已在 `.gitignore` 中忽略）。
- 不要手改 `Output/bin/xcfg.*` 和 `Output/bin/res.*`，它们由 prebuild 与配置工具重新生成。
- 切换不同测试任务（A1/A2/A3/A4）时，修改 `app/projects/standard/main.c` 的 `xcfg_cb.test_mode = TEST_xxx` 那一行（注释掉恢复 xcfg 配置选择）。

---

## 提交历史（最近 10 次）

```text
50d67e0 chore: catch-up A1 step-3 macros and reference WAV
1ba0dcc feat(a2-pc12): tune noise/avol/samples, A2 passes 1kHz stable output
ca70a1d feat(a2): implement auxadc_pcm_to_dac() via --wrap, AUX on PE6/PE7
7ee1cf3 docs: add zero-basics BT8920A2 audio experiment tutorial
a22cf4b docs(a1-pc3/pc4): correct avol index table (avol=50 -> N_4DB = 0x02)
3ee092f feat(a1-step2): switch sine table to 32-sample/500 Hz (sine_16k_500hz)
462d11f feat(a1-step1b): raise analog volume to 50 (-5 dB) for clearer listening
decc433 feat(a1-step1): switch DAC sample rate to 16 kHz (SPR_16000)
f640541 chore: initial commit of BT8920A2 SDK workspace
be4623c Initial commit (remote)
```