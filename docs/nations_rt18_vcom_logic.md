# Nations RT_Thread18 Virtual COM 运行逻辑

> **范围：** 官方例程 `Nations.N32WB452_Library.2.6.0/.../RT_Thread18_Virtual_COM_Port`  
> **日期：** 2026-08-24  
> **用途：** 把例程的时钟 / CherryUSB / CDC 路径写清，供产品工程 `gcc-rt-cursor` 对照。  
> **本次不在 `gcc-rt-cursor` 里改 USB、不做联调。**

产品侧已有真实 VCOM：`services/usb`（Nations **usbfs** 栈）+ CLI / ulog / 透传。本例程是 **CherryUSB + 演示收发**，**没有 USART↔USB 桥**。两边只共享：N32WB452 FS USB IP、48M USB 时钟、DP 内部上拉。

---

## 1. 例程实际在干什么

主机插上后会出现 **Nations N32WB452 Port**（VID `0x19F5` / PID `0x5245`），像一个 COM 口。

数据路径：

| 方向 | 例程行为 | 不是 |
|------|----------|------|
| PC → 设备（OUT EP 0x02） | ISR 里读最多 64 字节，用 `rt_kprintf` 打十六进制到 **USART1 调试口** | 不转发给任何业务 UART |
| 设备 → PC（IN EP 0x81） | `main` 每 500ms 发固定 10 字节 `123456789:` | 不读 USART1，也不等 DTR |

`dtr_enable` 只在 `usbd_cdc_acm_set_dtr()` 里置位，**发送函数不判断 DTR**。函数名 `cdc_acm_data_send_with_dtr_test` 是误导。

CDC `SET_LINE_CODING`（波特率等）只存在类驱动结构体里，**不改任何硬件 UART**。虚拟串口的“波特率”对 USB 全速 bulk 无意义。

---

## 2. 启动顺序

```text
复位
  → SystemInit()                    firmware/CMSIS/device/system_n32wb452.c
       HSI 就绪 → 切 HSI
       开 HSE → 等 HSERDY
       PLL：HSE/2 × 9 = 144M（HSE_VALUE=32M）或 HSE×18=144M（HSE=8M）
       切 SYSCLK=PLL；失败则退回 HSI 8M 并 return（后面 USB 必挂）
  → C 运行时 → $Sub$$main / 入口
       rtthread_startup()
         rt_hw_board_init()         本例 src/board.c
           NVIC 向量、SysTick=10ms、堆、console=usart1
         定时器 / 调度器
         建 main 线程
         rt_system_scheduler_start()
  → main 线程
       rt_components_init()         注册 USART1 等 INIT_BOARD
       main()
         cdc_acm_init()
           注册描述符 / CDC 接口 / EP 回调
           usbd_initialize() → usb_dc_init()
             usb_dc_low_level_init()   本例 src/main.c
               确认 SYSCLK 已是 PLL（0x08），否则死等
               USBCLK = PLL/3 = 48M，开 APB1 USB
               USB_LP + USBWakeUp + EXTI18
               DP 内部上拉（0x40001820 bit28）
             USB IP 复位、BTABLE、CTR/RESET 中断
         while(1)
           发 10 字节演示包
           rt_thread_delay(50)      50 tick × 10ms = 500ms
```

USB 业务几乎全在 **`USB_LP_CAN1_RX0_IRQHandler`**（CherryUSB 里宏成 `USBD_IRQHandler`），不在独立 USB 线程里。

---

## 3. 时钟（USB 能枚举的前提）

USB FS 必须 **48MHz ±0.25%**。本芯片 USBCLK 只能来自 **PLL 分频**。

| 项 | 值 |
|----|-----|
| 本板 MCU HSE | OSC_IN/OUT **32M 晶振**（不是 BLE 射频 32M） |
| `HSE_VALUE` | `32000000`（`n32wb452.h` + Keil/IAR 宏） |
| `SYSCLK_SRC` | `SYSCLK_USE_HSE_PLL`（必须 PLL，不能只用 HSE） |
| `SYSCLK_FREQ` | `144000000` → `32M/2 × 9` |
| USBCLK | `RCC_USBCLK_SRC_PLLCLK_DIV3` → 144/3=48M |

HSE 起振失败：`SetSysClock()` 设 `SystemCoreClock=HSI` 后直接 return，PLL 不配。`usb_dc_low_level_init()` 会卡在 `RCC_GetSysclkSrc()!=0x08`。

Type-C 只做 USB2.0 时：

| Type-C | USB | MCU |
|--------|-----|-----|
| A6 / B6 | D+ | USBDP |
| A7 / B7 | D- | USBDM |

A6 与 B6 短接、A7 与 B7 短接，才能正反插。

---

## 4. CDC 描述符与端点

文件：`RT_Thread18_Virtual_COM_Port/src/cdc_acm.c`

| 项 | 值 | 作用 |
|----|-----|------|
| 设备类 | `0x02` Communication | 主机走 CDC 驱动 |
| 配置 | 2 个接口、总线供电、100mA | |
| IF0 | CDC 通信（Header/ACM/Union/CallMgmt） | EP **0x83** INT，通知 |
| IF1 | CDC 数据 | EP **0x02** OUT bulk、**0x81** IN bulk |
| 字符串 | NATIONS / N32WB452 Port | 设备管理器显示名 |

`cdc_acm_init()`：

1. `usbd_desc_register(cdc_descriptor)`
2. `usbd_cdc_add_acm_interface` 两次：命令 IF + 数据 IF（同一 class）
3. 数据 IF 挂 `cdc_out_ep` / `cdc_in_ep` 回调
4. `usbd_initialize()` 打开 USB IP

---

## 5. 枚举（主机认 COM）

物理：`usb_dc_low_level_init` 拉 DP → 主机看成 FS 设备 → 复位。

```text
USB ISTR RESET
  → USBD_EVENT_RESET
      地址=0，开 EP0 IN/OUT
      CDC reset：默认 line_coding 2Mbps 8N1

GET_DESCRIPTOR Device / Config / String
SET_ADDRESS
  → 地址写到 USB->DADDR（等 EP0 IN 0 长度完成）

SET_CONFIGURATION(1)
  → 扫描述符，按 EP 描述符 usbd_ep_open（bulk/int、MPS、PMA）
  → USBD_EVENT_CONFIGURED
  → 主机加载 usbser / CDC ACM，出现 COM
```

打开串口助手后常见类请求（`usbd_cdc.c`）：

| bRequest | 含义 | 例程处理 |
|----------|------|----------|
| `SET_LINE_CODING` | 波特率/停止位/校验/数据位 | 存 RAM，调空的 weak `usbd_cdc_acm_set_line_coding` |
| `GET_LINE_CODING` | 回读 | 返回 RAM |
| `SET_CONTROL_LINE_STATE` | DTR=bit0，RTS=bit1 | 调 `usbd_cdc_acm_set_dtr`（本例只改 `dtr_enable`） |

控制传输走 EP0，在 `USBD_IRQHandler` 里 `SETUP` / `EP0_IN` / `EP0_OUT`。

---

## 6. 数据面（所谓“转串口”）

### 6.1 PC → 设备

```text
主机 bulk OUT → EP 0x02
  USB ISTR CTR、DIR=OUT
  → USBD_EVENT_EP_OUT_NOTIFY(0x02)
  → usbd_ep_out_handler 找到 cdc_out_ep.ep_cb
  → usbd_cdc_acm_out()
       usbd_ep_read(ep, buf, 64, &n)   从 PMA 拷到 SRAM
       循环 rt_kprintf("%02x ")        出 USART1
       usbd_ep_read(ep, NULL, 0, NULL) 把 EP RX 置 VALID，允许下一包
```

注意：`rt_kprintf` 在 **USB ISR** 里调，可能阻塞或重入，产品代码不要抄。

### 6.2 设备 → PC

```text
main 线程
  cdc_acm_data_send_with_dtr_test()
    usbd_ep_write(0x81, "123456789:", 10, NULL)
      数据写入 PMA，EP TX VALID
  主机 IN token 取走
  USB CTR TX
  → USBD_EVENT_EP_IN_NOTIFY(0x81)
  → usbd_cdc_acm_in() 只打印 "in"
```

未配置或主机没读 IN 时，反复 `usbd_ep_write` 可能覆盖/失败，例程不管。

### 6.3 和产品 VCOM 的差别

```text
例程（CherryUSB）
  PC ──USB── CDC 演示包 / hex 打印 ── USART1 调试口
  （GNSS/RDSS UART 完全不在这条链上）

产品 gcc-rt-cursor（Nations usbfs）
  PC ──USB── services/usb/cdc_acm.c
                ├─ DTR 后 CLI JSON（services/cli）
                ├─ ulog 后端（services/log）
                └─ PASSTHRU：GNSS/RDSS UART ↔ 同一 CDC（services/stream）
```

产品环形缓冲、`USB_To_USART_Send_Data`、`Handle_USBAsynchXfer` 见 `gcc-rt-cursor/services/usb/cdc_acm.c` 与 `flow.md`。名字沿官方 Virtual_COM，实际 USART 缓冲已改成给 CLI/ulog/透传用。

---

## 7. 中断与底层文件

| 符号 | 文件 | 作用 |
|------|------|------|
| `USB_LP_CAN1_RX0_IRQHandler` | CherryUSB `usb_dc_fsdev.c`（`USBD_IRQHandler`） | CTR/RESET/SUSP/WKUP 等 |
| `USBWakeUp_IRQHandler` | 本例 `n32wb452_it.c` | 清 EXTI18 |
| `usb_dc_low_level_init` | 本例 `main.c` | 时钟、NVIC、DP 上拉 |
| `usb_dc_init` | `usb_dc_fsdev.c` | IP 复位、开中断 |
| `usbd_event_notify_handler` | `usbd_core.c` | 事件分发 |
| `cdc_acm_class_request_handler` | `usbd_cdc.c` | CDC 类请求 |

N32WB452 USB 是 STM32F1 风格 **FS Device + 512B PMA**。CherryUSB 端口 `port/fsdev` 与产品 `firmware/n32wb452_usbfs_driver` 操作同一套寄存器，协议栈 API 不同。

DP 上拉：`*(0x40001820) |= 0x10000000`。不拉主机看不到设备。产品在栈就绪后再拉（见 `services/usb/README.md`）。

---

## 8. 例程文件地图

```text
RT_Thread18_Virtual_COM_Port/
  src/main.c          入口、USB 时钟/中断/上拉（弱符号 usb_dc_low_level_init）
  src/cdc_acm.c       描述符、init、OUT/IN/DTR、演示发送
  src/board.c         SysTick、堆、console
  src/n32wb452_it.c   USBWakeUp；USB_LP 不在这里
  inc/rtconfig.h      RT-Thread；console=usart1，tick=10ms

firmware/CMSIS/device/system_n32wb452.c   SYSCLK
firmware/CMSIS/device/n32wb452.h          HSE_VALUE
middlewares/rt-thread/components/drivers/cherryusb/
  core/usbd_core.c
  class/cdc/usbd_cdc.c
  port/fsdev/usb_dc_fsdev.c
```

---

## 9. 对照产品时注意

1. **不要把本例的 `cdc_acm.c` 整份拷进 `gcc-rt-cursor`。** 产品已是 Nations usbfs + 环形缓冲 + 回调。
2. 可复用的只有：HSE 32M→PLL 144M→USB/3=48M、DP 上拉时机、Type-C A6/B6=D+、A7/B7=D-。
3. 例程 ISR 里 `rt_kprintf`、不等 DTR 狂发 IN，产品里都不要学。
4. 产品插 USB 会 `pm_lock` / CHARGE 或透传，避免 STOP 把 USB 掐死（`board_bringup.md`）。
5. 例程能枚举、产品不能：先对时钟与 DP 上拉，再对两套栈的 `USB_Init` / 描述符 / 端点 PMA。

产品 USB 文档：[`services/usb/README.md`](../services/usb/README.md)、[`services/usb/flow.md`](../services/usb/flow.md)。
