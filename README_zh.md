[English](README.md)

# TinyUSB-CMSIS-DAP-F401 — STM32F401CC 调试探针固件

用 **TinyUSB** + **ARM CMSIS-DAP** 源码在 **STM32F401CCU6** 上实现的一个
**CMSIS-DAP v2 调试探针**，同时提供一路 **CDC-ACM 虚拟串口**（桥接到 USART1）。

免驱：Windows 10/11 上通过 Microsoft OS 2.0 描述符自动绑定 **WinUSB**，
Keil MDK、pyOCD、OpenOCD 等工具无需安装任何驱动即可直接识别。

实测硬件：WeAct MiniF4 V3.1 布局（STM32F401CCU6，25 MHz 晶振）。

## 与 DAPLINK-STM32F401CC 的区别

本仓库与 [DAPLINK-STM32F401CC](https://github.com/liang67/DAPLINK-STM32F401CC) 是
**两条独立的技术路线**，功能目标相同（CMSIS-DAP v2 + CDC），实现方式不同：

| | 本仓库 | DAPLINK-STM32F401CC |
|---|---|---|
| 代码基础 | ARM **CMSIS-DAP** 源码（`DAP.c` / `SW_DP.c`） | **ARMmbed/DAPLink** 移植 |
| USB 栈 | **TinyUSB** 0.18.0 | DAPLink 自带 USB 栈 |
| 构建系统 | 仅 Keil MDK（ARMCLANG） | CMake (GCC) **和** Keil |
| SWD 引脚 | **PA13 / PA14**（与 MCU 自身调试口复用） | PB13 / PB14（独立引脚） |

## 功能

| 功能 | 实现 |
|---|---|
| 调试探针 | CMSIS-DAP **v2.1.1**，`DAP_PACKET_SIZE = 64`，`DAP_PACKET_COUNT = 2` |
| WinUSB | BOS 描述符 + Microsoft OS 2.0 描述符（Compatible ID `WINUSB`） |
| 串口 | CDC-ACM ↔ **USART1**（PA9 TX / PA10 RX，AF7） |
| 状态指示 | **PC13** 单颗 LED，低电平点亮，心跳 / 连接 / 错误共用 |
| 目标复位 | **PB0** 开漏输出，`DAP_ResetTarget` 产生 20 ms 低脉冲 |

VID/PID 沿用 mbed 分配给 DAPLink 的 `0x0D28` / `0x0204`，`bcdDevice = 0x0104`。

### 只做 SWD，不做 JTAG

`DAP_JTAG = 0`：板上没有引出 TDI/TDO/nTRST。`DAP_SWJ_Pins` 会无条件引用这些钩子，
因此 `DAP_config.h` 里提供了无害的空实现。

## 引脚分配

| 引脚 | 功能 | 说明 |
|---|---|---|
| **PA13** | SWDIO / TMS | 推挽输出（或上拉输入），驱动目标 |
| **PA14** | SWCLK / TCK | 推挽输出，驱动目标 |
| **PB0** | nRESET | 开漏输出 + 内部上拉，低电平复位目标 |
| **PC13** | 状态 LED | 低电平点亮 |
| PA9 | USART1_TX | AF7 |
| PA10 | USART1_RX | AF7 |
| PA11 | USB_OTG_FS_DM | AF10 |
| PA12 | USB_OTG_FS_DP | AF10 |

## ⚠️ 重要：PA13/PA14 与本机调试口冲突

**PA13/PA14 正是这颗 STM32F401 自己的 SWD 调试口。** 固件一旦把它们配成 GPIO
去驱动目标芯片，外部 J-Link/ST-Link 就再也 attach 不上本芯片了（USB 功能完全正常，
只是烧不了程序）。

为此固件在 `main()` 里加了一个**启动调试窗口**：

```c
/* Core/Src/main.c, USER CODE BEGIN SysInit */
#if (DAP_BOOT_DEBUG_DELAY_MS > 0U)
  HAL_Delay(DAP_BOOT_DEBUG_DELAY_MS);
#endif
```

`DAP_BOOT_DEBUG_DELAY_MS` 定义在 [`Core/Inc/DAP_config.h`](Core/Inc/DAP_config.h)，
默认 **2000 ms**，位于 `SystemClock_Config()` 之后、`MX_GPIO_Init()` 之前。
也就是说**每次复位后的 2 秒内**调试器仍可 attach 并重新烧录。超过 2 秒固件接管引脚。

- 连不上时按一下板上的 **RESET** 键，再立刻连（大多数调试器 2 秒足够）。
- J-Link OB 等连接较慢的工具，可把该值调大到 `5000`。
- 正式量产固件可设为 `0` 关掉窗口 —— **但那样就只能靠 BOOT0 进 DFU 才能重烧**。

如果已经锁死，用 [`tools/recover_dfu.ps1`](tools/recover_dfu.ps1)：

```powershell
# 1. 按住 BOOT0，点一下 RESET，0.5 秒后松开 BOOT0
# 2. 运行（默认烧 MDK-ARM 输出目录里的 hex）
.\tools\recover_dfu.ps1
```

## 硬件设计要求

### 内部 D+ 上拉，无需外部上拉电阻

`STM32F401` 的 OTG_FS 有内部 D+ 上拉，由 dwc2 控制器清 `DCTL.SDIS` 使能
（`dcd_connect()`）。**不要**再外接 1.5 kΩ 上拉电阻。

注意 `STM32F401` 没有 DPPU 位（不像 F1 系列），GCCFG 里只有 VBUS 相关控制位。

### VBUS 检测必须关闭（本板特有）

`STM32F401` 的 OTG_FS **VBUS 检测输入复用 PA9**，而本设计把 PA9 用作
`USART1_TX`。若不关闭 VBUS 检测，硬件永远认为"没插主机"，D+ 上拉不使能，
**主机就完全看不到设备**（这正是开发中遇到的第一个大坑）。

TinyUSB 的 dwc2 端口只写 `GCCFG.PWRDWN`，从不碰 `NOVBUSSENS`，所以固件自己补上：

```c
/* Core/Src/usb_otg.c */
void usb_otg_disable_vbus_sensing(void)
{
  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_NOVBUSSENS;
  USB_OTG_FS->GCCFG &= ~(USB_OTG_GCCFG_VBUSASEN | USB_OTG_GCCFG_VBUSBSEN);
}
```

在 `usb_otg_clock_gpio_init()` 里调用一次，并在 `tusb_init()` **之后**再调一次
（dwc2 的 core reset 会清 GCCFG）。

## 端点与接口

| 接口 | 类 | 端点 |
|---|---|---|
| 0 | CDC Control | `0x81` IN（notify，8 字节） |
| 1 | CDC Data | `0x02` OUT / `0x82` IN |
| 2 | Vendor (WinUSB) | `0x03` OUT / `0x83` IN |

F401 的 FS 只有 EP0..EP3，上面这套正好用满。

## 序列号规则

序列号 = **`"L67"` + 96 位芯片 UID 的十六进制**，共 **27 个字符**：

```
L67 003300543038510932313531
└┬┘ └──────────┬──────────┘
 │             └─ UID (0x1FFF7A10, 3 个字，高字节在前)
 └─ 固定前缀
```

UID 每颗芯片唯一且永不改变，所以**升级固件不会改变探针名字**。
固件版本号放在 `bcdDevice` 字段（`0x0104`），这也是主机的常规做法。

> 开发过程中曾把 `bcdDevice` 编进序列号，用来强制 Windows 生成全新的设备实例、
> 绕开被 MS OS 2.0 描述符"毒化"的旧节点。现已移除。

## 编译

需要 **Keil MDK-ARM 5** + **Arm Compiler 6**（开发时用的是 MDK Plus 5.41 / AC6.22）
以及 `Keil.STM32F4xx_DFP` 包。

```powershell
# 命令行编译
& 'C:\Keil_v5\UV4\UV4.exe' -r 'MDK-ARM\00-TINYUSB-DAPLINK.uvprojx' -j0 -o build.log
```

或者直接用 Keil 打开 `MDK-ARM/00-TINYUSB-DAPLINK.uvprojx` 编译。

当前体积（`.uvprojx` 里 `Optim` = 3，AC6 对应 `-O2`）：

```
RW_IRAM1  0x20000000  size 0x00010000  used 8.0 KB  / 64 KB   (12.6%)
ER_IROM1  0x08000000  size 0x00040000  used 32.2 KB / 256 KB  (12.6%)
Stack 0x800 (2 KB), Heap 0x200
```

栈在 `startup_stm32f401xc.s` 里从默认 1 KB 加大到 2 KB —— TinyUSB 设备栈 +
HAL 回调在枚举阶段需要。

产物在 `MDK-ARM/00-TINYUSB-DAPLINK/`：
`00-TINYUSB-DAPLINK.hex`（烧录用）、`.bin`（DFU/其他工具用）、`.axf`（调试用）。

## 烧录

### 1. SWD（推荐）

```powershell
# J-Link
JLink.exe -device STM32F401CC -if SWD -speed 4000 -autoconnect 1 `
          -CommanderScript flash.jlink
# flash.jlink 内容：
#   r
#   h
#   loadfile "MDK-ARM\00-TINYUSB-DAPLINK\00-TINYUSB-DAPLINK.hex"
#   r
#   g
#   q
```

**记得在复位后 2 秒内连接**（见上文"PA13/PA14 冲突"）。

### 2. DFU（芯片已被本次固件锁死时的救急手段）

```powershell
.\tools\recover_dfu.ps1
```

脚本会等 `USB\VID_0483&PID_DF11`（STM32 系统 bootloader）出现，
然后调用 `STM32_Programmer_CLI` 烧录并校验。

## 验证工具

`tools/` 下三个 PowerShell 脚本，直接跟运行中的探针对话（无需 Keil/pyOCD）：

```powershell
# 读 DAP_Info（厂商/产品/序列号/固件版本/能力/包大小）
.\tools\dap_probe.ps1

# 发任意 CMSIS-DAP 请求并打印响应
.\tools\dap_cmd.ps1 -Hex "00 F0", "02 00", "10 80 80"

# DFU 救砖
.\tools\recover_dfu.ps1
```

`dap_cmd.ps1` 通过 WinUSB 打开 MI_02 接口（GUID
`{CDB3B5AD-293B-4663-AA36-1AAE46463776}`），OUT 管道 `0x03`、IN 管道 `0x83`，
请求自动补零到 64 字节。

### 参考输出

```
TX 00-F0      -> RX[4]  00-02-31              # DAP_Info(Capabilities) = SWD|Atomic|Timestamp
TX 00-01      -> RX[10] 00-08-"Liang67"       # 厂商名
TX 00-02      -> RX[30] 00-1C-"Liang67 STM32F401CC DAPLink"
TX 00-03      -> RX[30] 00-1C-"L6700..."      # 序列号，27 字符
TX 00-04      -> RX[8]  00-06-"2.1.1"         # DAP 协议版本
TX 02-00      -> RX[2]  02-01                 # DAP_Connect → SWD
TX 10-83-83   -> RX[2]  10-A3                 # SWJ_Pins: SWCLK/SWDIO 均高
TX 10-00-83   -> RX[2]  10-20                 # SWJ_Pins: SWCLK/SWDIO 均低
TX 10-80-80   -> RX[2]  10-A1                 # SWJ_Pins: 释放 nRESET
```

## 已知问题与实现细节

这几点都是开发过程中真踩过的坑，改代码时请留意：

### 1. 不要用 `tud_vendor_rx_cb()`

TinyUSB 头文件里该回调被声明为 `TU_ATTR_WEAK`，**我们的定义也会变成弱符号**。
armlink 判定"未被强引用"，会把这个函数整体删掉：

```
Removing dap_link.o(.text.tud_vendor_rx_cb), (132 bytes).
```

结果就是 **所有 vendor OUT 数据被静默丢弃**（USB 枚举、CDC 全部正常，只是 DAP 不响应）。
本工程改为在 `dap_process_packet()` 里**轮询** `tud_vendor_available()` 填充队列。
CDC 桥本来就用的轮询（`tud_cdc_available()`），所以没受影响。

### 2. MS OS 2.0 描述符里的 `bConfigurationValue` 必须写 0

Configuration subset header 的 `bConfigurationValue` 若写成 `1`，Windows 会取回
完整描述符集后**丢弃** compatible ID，表现为 vendor 接口报
**Code 28 (CMRE_PROB_FAILED_INSTALL)**、`ConfigFlags = 64`，
`CompatibleIDs` 缺 `USB\MS_COMP_WINUSB`。

### 3. `DeviceInterfaceGUIDs` 必须是 `REG_MULTI_SZ`

`wPropertyDataType` 要写 `0x0007`，不是 `0x0001`。
GUID 用 CMSIS-DAP 探针惯用的 `{CDB3B5AD-293B-4663-AA36-1AAE46463776}`。

### 4. 被"毒化"的设备节点不会自愈

Windows 只在**首次**看到设备时请求一次 OS 描述符，把结果缓存在
`HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<VID><PID><bcdDevice>`。
一旦那次失败（`osvc=0,0`），后面无论如何都拿不到 WinUSB。
解决办法只有改动**序列号或 bcdDevice**，让 Windows 建一个全新实例。

### 5. CDC 桥不要求 DTR

`tud_cdc_connected()` 要求主机置 DTR，很多串口工具默认不置。
本工程用 `tud_cdc_ready()`，插上就能收数据。

### 6. 8 数据位 + 校验位要用 `UART_WORDLENGTH_9B`

STM32 的校验位计入帧长。`tud_cdc_line_coding_cb()` 里按
`Parity == UART_PARITY_NONE ? 8B : 9B` 处理。

## 目录结构

```
Core/Inc/
  DAP_config.h          板级配置：引脚、时钟、能力、延时、LED 钩子
  dap_link.h            DAP 链路 + 串口桥的对外接口
  tusb_config.h         TinyUSB 配置（CDC + Vendor）
  usb_descriptors.h     描述符与字符串索引
Core/Src/
  dap_link.c            DAP 包处理、CDC↔USART1 桥、LED 状态机
  usb_descriptors.c     设备/配置/BOS/MS OS 2.0/字符串描述符
  main.c                CubeMX 骨架 + 启动调试窗口
  usb_otg.c             OTG_FS 时钟、GPIO、关闭 VBUS 检测
  gpio.c                引脚初始化
  usart.c               USART1 115200 8N1
  stm32f4xx_it.c        中断（OTG_FS 走 tud_int_handler）
MDK-ARM/                Keil 工程
Middlewares/Third_Party/
  tinyusb/              TinyUSB 0.18.0（已裁剪，仅保留 src/）
  cmsis-dap/            ARM CMSIS_5 5.9.0 的 DAP 源码
tools/                  验证与救砖脚本
```

## 第三方组件

| 组件 | 版本 | 许可证 |
|---|---|---|
| [TinyUSB](https://github.com/hathach/tinyusb) | 0.18.0 | MIT |
| [CMSIS-DAP](https://github.com/ARM-software/CMSIS_5) | 5.9.0 | Apache-2.0 |
| [STM32F4 HAL](https://github.com/STMicroelectronics/stm32f4xx_hal_driver) | CubeMX F4 1.8.x | BSD-3-Clause |
| [CMSIS](https://github.com/ARM-software/CMSIS_5) | 5.x | Apache-2.0 |

## 参考资料

- [CMSIS-DAP 规范](https://arm-software.github.io/CMSIS_5/DAP/html/index.html)
- [Microsoft OS 2.0 描述符规范](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification)
- [TinyUSB 文档](https://docs.tinyusb.org/)

## 许可证

本项目代码采用 **Apache License 2.0**，见 [LICENSE](LICENSE)。
第三方组件遵循各自许可证（见上表）。
