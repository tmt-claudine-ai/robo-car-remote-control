# 当前结论：Phase 1 已具备正式落地条件

## 结论摘要

基于新增参考代码 [esp32WG.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/doc/esp32WG.ino)、[uploader.html](h:/Work/TwinMatrix/Projects/Mixly/SVCar/doc/uploader.html) 以及补充说明：

- ATmega328P 的 `UART0` 也就是 `D0/D1`
- ESP32-S3 通过主板引出的 4pin 线可直接接入该 `UART0`
- ESP32-S3 另有一根线接到 328P 的 `RESET`

目前已经有足够信息，按下面这条链路尝试完整 Phase 1：

```text
PC -> MQTT Broker -> ESP32-S3 -> UART0(D0/D1 on 328P) -> Nano runtime -> 底盘
```

这和此前基于 `D11/D12` 自定义桥接的假设不同。新的 `UART0` 路线有明确参考代码支撑，已经成为当前主路线。

## 已确认的信息

### 1. 网络侧链路已经打通

已经实际验证：

- 本地 MQTT broker 可运行
- PC 端 teleop 工具可运行
- ESP32-S3 可以连接 Wi‑Fi
- ESP32-S3 可以连接 MQTT broker
- `claim`、`cmd`、`stop`、`release` 流程正常

相关文件：

- [mqtt_teleop.py](h:/Work/TwinMatrix/Projects/Mixly/SVCar/tools/mqtt_teleop.py)
- [local_broker.py](h:/Work/TwinMatrix/Projects/Mixly/SVCar/tools/local_broker.py)
- [virtual_car.py](h:/Work/TwinMatrix/Projects/Mixly/SVCar/tools/virtual_car.py)
- [esp32_mqtt_bridge.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_mqtt_bridge/esp32_mqtt_bridge.ino)

### 2. 官方 Wi‑Fi 烧录参考代码确认了主板链路

[esp32WG.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/doc/esp32WG.ino) 里可以直接看到：

- `LINK_UART_RX = 6`
- `LINK_UART_TX = 7`
- `LINK_RESET_PIN = 4`
- `LINK_UART_BAUD = 115200`

这个程序的用途是让 ESP32 通过 STK500v1 给 328P 烧录固件。它能成立，说明至少这两件事是真的：

- ESP32 能访问 328P 的硬串口 `UART0`
- ESP32 能控制 328P 的复位线

这正是完整 Phase 1 最关键的硬件基础。

### 3. Tai_finder_X1 官方库的串口入口也指向 `Serial`

从 [TAI_finder_X1.cpp](h:/Work/TwinMatrix/Projects/Mixly/SVCar/doc/Tai_finder_X1/TAI_finder_X1.cpp) 可以确认：

- `start_serial_command()` 使用 `Serial`
- `Bluetooth_Connect()` 也使用 `Serial`
- `tmd ...` 是这套官方库识别的一类串口控制命令

这说明主板的官方控制思路本来就是：

```text
外部模块 -> 328P 的硬串口 Serial(D0/D1)
```

### 4. 真实主板底盘引脚已经明确

根据 [TAI_finder_X1_config.h](h:/Work/TwinMatrix/Projects/Mixly/SVCar/doc/Tai_finder_X1/TAI_finder_X1_config.h)：

- 左前：`DIR D7`，`PWM D9`
- 左后：`DIR D8`，`PWM D10`
- 右前：`DIR D13`，`PWM D3`
- 右后：`DIR D12`，`PWM D11`

这也解释了为什么此前把 `D11/D12` 当串口桥候选是错误方向。

## 先前 probe 结果如何重新解释

之前做过 `atmega_link_probe` / `esp32_link_probe`，没有找到稳定命中。

现在看，这个结果不能再被解读为“主板里没有 ESP32 -> Nano 链路”，更准确的解释是：

- probe 主要围绕早期自定义桥接假设展开
- 真实链路更可能是 `328P UART0(D0/D1)`
- 而 `D0/D1` 同时又容易被 USB 串口监视、烧录链路干扰

所以 probe 失败只能说明：

- 早期假设不成立

不能说明：

- `UART0` 这条正式链路不存在

## 当前正式实现路线

仓库里已经新增了这套基于 `UART0` 的 Phase 1 固件：

### Nano 侧

- [nano_phase1_uart0_runtime.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/nano_phase1_uart0_runtime/nano_phase1_uart0_runtime.ino)

特点：

- 使用 `Serial(D0/D1)`
- 使用 `Tai_finder_X1` 主板对应的真实底盘引脚
- 接收：
  - `CMD,seq,left,right,ttl`
  - `STOP,reason`
  - `PING`
- 返回：
  - `READY,...`
  - `ACK,...`
  - `ACK,STOP`
  - `PONG,...`
  - `TEL,...`
- 包含 deadman timeout 停车

### ESP32 侧

- [esp32_uart0_phase1_bridge.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/esp32_uart0_phase1_bridge.ino)
- [config.h](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/config.h)

特点：

- 继续使用已验证的 MQTT topic / ownership / heartbeat 逻辑
- 改为走：
  - `GPIO6` / `GPIO7` 作为板间 UART
  - `GPIO4` 作为可选 reset 控制
- 把 MQTT 控制命令转发给 328P `UART0`
- 读取并转发 `READY` / `ACK` / `PONG` / `TEL`

## 当前仍然存在的风险

虽然 Phase 1 已经具备正式落地条件，但还保留几个现实风险：

- Nano 的 `Serial(D0/D1)` 同时也是 USB 串口，调试时可能互相干扰
- 主板上如果有其他模块也并在这条 `UART0` 上，可能出现串口竞争
- 官方 `TAI_finder_X1` 库的 `tmd ...` 协议与当前自定义 `CMD,...` 运行时是两条不同路线
- 还没有在真实底盘上完成最终闭环验证

这些风险不会否定当前路线，只意味着 bring-up 时要优先做真机验证，而不是继续猜测硬件。

## 当前最合理的项目状态描述

建议现在对项目状态这样表述：

### 已完成

- MQTT 网络控制链路验证
- PC 控制端工具
- 本地 broker
- 虚拟车仿真
- ESP32-S3 MQTT 桥接验证
- 328P 真实板间链路识别
- 基于 `UART0` 的 Phase 1 固件初版

### 正在进行

- 用 `GPIO6/GPIO7 + GPIO4` 实际打通 ESP32-S3 -> 328P -> 底盘

### 尚未最终验证

- 真车底盘动作闭环
- 复杂场景下的串口竞争与稳定性

## 下一步

当前最值得做的不是继续写假设文档，而是直接进行 bring-up：

1. 给 328P 烧录 [nano_phase1_uart0_runtime.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/nano_phase1_uart0_runtime/nano_phase1_uart0_runtime.ino)
2. 给 ESP32-S3 烧录 [esp32_uart0_phase1_bridge.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/esp32_uart0_phase1_bridge.ino)
3. 在 [config.h](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/config.h) 中填入 Wi‑Fi / MQTT 配置
4. 启动 broker 和 teleop
5. 在 ESP32 串口监视器观察：
   - `ROBOT READY,...`
   - `ROBOT TEL,...`
   - `ROBOT ACK,...`

如果这些日志出现，就说明 Phase 1 的核心硬件链路已经真正打通。
