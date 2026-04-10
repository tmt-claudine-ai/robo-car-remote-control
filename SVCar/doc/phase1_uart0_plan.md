# Phase 1 UART0 联调方案

## 依据

新增参考代码 [esp32WG.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/doc/esp32WG.ino) 表明：

- ESP32-S3 使用 `UART2`
- 板间链路引脚为：
  - `GPIO6`
  - `GPIO7`
- 复位控制引脚为：
  - `GPIO4`

再结合 [TAI_finder_X1.cpp](h:/Work/TwinMatrix/Projects/Mixly/SVCar/doc/Tai_finder_X1/TAI_finder_X1.cpp) 中 `start_serial_command()` 依赖 `Serial(D0/D1)`，目前最合理的正式链路是：

```text
PC -> MQTT Broker -> ESP32-S3 -> UART0(D0/D1 on 328P) -> 328P runtime -> 底盘
```

## 新增固件

### 328P / Nano 运行时

- [nano_phase1_uart0_runtime.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/nano_phase1_uart0_runtime/nano_phase1_uart0_runtime.ino)

这份草图：

- 使用 `Serial(D0/D1)`
- 使用 `Tai_finder_X1` 主板对应的底盘引脚
- 支持：
  - `CMD,seq,left,right,ttl`
  - `STOP,reason`
  - `PING`
- 返回：
  - `READY,...`
  - `ACK,...`
  - `ACK,STOP`
  - `PONG,...`
  - `TEL,...`
- 包含 deadman timeout 自动停车

### ESP32-S3 桥接程序

- [esp32_uart0_phase1_bridge.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/esp32_uart0_phase1_bridge.ino)
- [config.h](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/config.h)

这份草图：

- 沿用之前已验证的 MQTT topic / ownership 逻辑
- 使用：
  - `GPIO6/GPIO7` 作为板间 UART
  - `GPIO4` 作为可选 reset 控制
- 把 MQTT 轮速命令转发给 328P
- 读取并上报：
  - `READY`
  - `ACK`
  - `PONG`
  - `TEL`

## 配置步骤

先编辑 [config.h](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/config.h)：

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `MQTT_HOST`
- `MQTT_PORT`
- `MQTT_USERNAME`
- `MQTT_PASSWORD`
- `ROBOT_ID`

链路引脚默认已经按参考代码写好：

```cpp
LINK_UART_RX_PIN = 6
LINK_UART_TX_PIN = 7
LINK_RESET_PIN = 4
LINK_UART_BAUD = 115200
```

通常不需要再改。

## 烧录顺序

1. 给 328P / Nano 烧录 [nano_phase1_uart0_runtime.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/nano_phase1_uart0_runtime/nano_phase1_uart0_runtime.ino)
2. 给 ESP32-S3 烧录 [esp32_uart0_phase1_bridge.ino](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/esp32_uart0_phase1_bridge.ino)
3. 启动本地 broker
4. 运行 teleop

## 预期启动日志

ESP32 串口监视器应首先看到：

```text
ESP32 UART0 phase1 bridge boot
WIFI connected, IP=...
MQTT connected
```

如果 328P 板间链路正常，随后应看到：

```text
ROBOT READY,nano_phase1_uart0_runtime
ROBOT TEL,...
ROBOT PONG,...
```

当 PC teleop 发命令时，应看到：

```text
MQTT robot/car-01/cmd => ...
CMD,...
ROBOT ACK,...
ROBOT TEL,...
```

## 运行命令

本地 broker：

```powershell
.\scripts\start_local_broker.ps1
```

teleop：

```powershell
.\.venv\Scripts\python.exe .\tools\mqtt_teleop.py --broker <你的电脑局域网IP> --robot-id car-01
```

## 联调注意事项

### 1. Nano USB 串口可能会干扰 `D0/D1`

因为 328P 运行时现在直接使用 `Serial(D0/D1)`，所以 Nano 的 USB 串口监视器可能和板间链路竞争。

联调时优先级建议如下：

- 真机板间通信优先
- Nano USB 串口监视器只在必要时短时间使用

### 2. 初次测试建议架空车轮

第一次发运动指令时建议：

- 架空底盘
- 或断开电机机械负载

避免方向映射与预期不一致时直接冲车。

### 3. `PULSE_RESET_ON_BOOT`

[config.h](h:/Work/TwinMatrix/Projects/Mixly/SVCar/firmware/esp32_uart0_phase1_bridge/config.h) 里默认：

```cpp
PULSE_RESET_ON_BOOT = false
```

如果后续发现 328P 在 ESP32 启动后需要被主动拉一次复位，才会稳定进入运行时，可以再改成 `true` 做试验。

## 当前判定标准

出现下面三类日志，就可以认为 Phase 1 核心链路已打通：

- `ROBOT READY,...`
- `ROBOT ACK,...`
- `ROBOT TEL,...`

如果只看到 MQTT 日志，看不到任何 `ROBOT ...`，那说明网络链路通了，但板间串口还没真正跑起来。
