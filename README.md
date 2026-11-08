# ESP32 HID Web

通过手机网页向车机输入文字、发送按键和组合键。将刷好本项目固件的 ESP32-S3 开发板连接到车机 Type-C 接口，再用手机连接开发板热点，打开网页即可操作。

开发板会被识别为 USB HID 键盘，网页可离线使用，无需在车机安装接收程序。也可用于支持 USB 键盘的 Linux、macOS 和 Windows 设备。

原生 **ESP-IDF / CMake** 工程，使用 **`idf.py`** 编译、烧录和监控，提供：

- Linux、macOS、Windows 目标系统选择，以及各系统对应的 Unicode 输入模式。
- 英文、数字、符号、换行、Tab、方向键和常用组合键（含 Ctrl+Alt+F1）。
- **默认 0 秒开始延时**，点击发送后开始输入；可手动设置 0–10 秒延时，方便切换目标窗口。
- 发送进度、中途停止、USB 连接状态、Caps Lock 和 Num Lock 提示。
- Wi-Fi 热点、可选的局域网连接，以及 HTTP API。

中文输入依赖目标系统的输入方式，配置方法见「Unicode 输入方式」。

## 购买建议

- 建议购买 **ESP32-S3 开发板**，优先选择 **16 MB Flash、带原生 USB / OTG Type-C 接口**的型号，与本项目默认配置一致，不要求 PSRAM。
- 准备一根与开发板和车机接口匹配的 **USB 数据线**。
- 车机的 Type-C 接口需要支持 USB 数据传输和外接 USB 键盘。

## 快速开始

1. **购买开发板**：选择上述 ESP32-S3 开发板。
2. **刷入固件**：将本项目固件刷入开发板，具体步骤见下方「编译与烧录」。
3. **连接车机**：用 USB 数据线将开发板的原生 USB / OTG 接口连接到**车机 Type-C 接口**，等待开发板启动。
4. **手机连接热点**：在手机 Wi-Fi 设置中连接 SSID **`S3-HID-XXXXXX`**，**Wi-Fi 密码：`esp32hid123`**（默认值）。热点没有互联网，出现提示时选择保持连接。
5. **打开操作页面**：在手机浏览器访问 **[http://192.168.4.1](http://192.168.4.1)**，等待显示「USB 已连接」。
6. **开始操作**：在车机上点中要输入的文本框，在手机网页选择与车机匹配的目标系统和输入模式，输入文字后点击「发送到 USB」。也可以点击网页上的方向键、Enter 或组合键按钮。

**默认开始延时为 0 秒**，点击发送后立即开始输入。中文输入支持取决于车机系统和目标应用，使用前请参考下方「Unicode 输入方式」。

## 编译与烧录

使用 **ESP-IDF 6.x**；`dependencies.lock` 固定的 SDK 版本为 **6.0.3**。安装方式见 [ESP-IDF 官方入门文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html)。

先在 ESP-IDF 安装目录执行 `export.sh` 激活环境，Windows 可使用 ESP-IDF 命令行环境；再进入项目根目录：

```sh
idf.py --version
idf.py build
idf.py -p PORT -b 460800 flash
idf.py -p PORT monitor
```

将 `PORT` 替换为实际串口。Linux 常为 `/dev/ttyACM0` 或 `/dev/ttyUSB0`，macOS 常为 `/dev/cu.usbserial…` 或 `/dev/cu.usbmodem…`，Windows 常为 `COM3`。目标芯片默认为 `esp32s3`，也可显式运行 `idf.py set-target esp32s3`。首次构建会按 `main/idf_component.yml` 和 `dependencies.lock` 获取固定版本的托管组件。`-b 460800` 设置烧录波特率。退出 monitor 使用 `Ctrl+]`。

首次烧录可以使用 USB-to-UART 口。如果只有原生 USB 口，按住 **BOOT**、按一下 **RESET**、松开 BOOT，让芯片进入 ROM 下载模式，再烧录。烧录后按 RESET 运行。

**固件运行后，原生 USB 口只枚举 HID 键盘，没有串口。** 下一次通过原生 USB 烧录时，需要再次用 BOOT + RESET 进入下载模式。

网页由 CMake 的 `EMBED_TXTFILES` 自动嵌入固件，无需上传 SPIFFS / LittleFS。应用输出为 `build/esp32_hid_web.bin`；`idf.py flash` 按 `build/flash_args` 同时写入 bootloader、分区表和应用，不能把单独的应用 bin 当作整片镜像烧到地址 0。当前分区为 NVS（0x9000）、PHY（0xf000）、factory 应用（0x10000，3 MB）。

## 网页操作

「目标系统」应选择**车机或其他 USB 目标设备实际运行的系统**，与手机系统无关。英文输入使用美式英文键盘布局并关闭 Caps Lock；中文按下表准备目标输入方式，再选择对应 Unicode 模式。

| 目标系统 | 首次选择时的默认输入模式 | 编辑快捷键 | 中文输入准备 |
| --- | --- | --- | --- |
| Linux | Linux Unicode | Ctrl+A/C/V/Z | 目标应用支持 Ctrl+Shift+U |
| macOS | 美式键盘 | Command（⌘）+A/C/V/Z | 添加并选中 Unicode Hex Input |
| Windows | 美式键盘 | Ctrl+A/C/V/Z | 启用 EnableHexNumpad，重新登录，开启 Num Lock |

浏览器会记住目标系统及各系统最后选择的输入模式，不保存输入文字。系统和模式在任务期间不可更改；系统选择影响文字编码和四个编辑快捷键。Ctrl+Alt+F1 始终发送字面上的 Ctrl+Alt+F1。

可以使用同一台电脑打开网页和接收键盘输入：设置更长的开始延时，点击发送后切回目标输入框。方向键和常用按键点击后**立即**发送，不使用文字发送的开始延时。

每次最多 4096 个 UTF-8 字节，中文通常每字 3 字节，emoji 通常每个 4 字节。多行文字的换行会实际按 Enter，Tab 会实际按 Tab；它们也可能触发目标程序提交表单或切换焦点。「发送完毕后再按一次 Enter」默认关闭。

### Unicode 输入方式

USB HID 发送的是**物理键位和修饰键**，无法直接传输通用 Unicode 文本。三套系统的中文均依赖目标系统提供的输入方式。所有模式下的 ASCII 字符按美式键盘映射直接发送。

#### Linux

选择「Linux Unicode」后，非 ASCII 字符使用：

```text
你 → Ctrl+Shift+U → 4 → f → 6 → 0 → Enter
😀 → Ctrl+Shift+U → 1 → f → 6 → 0 → 0 → Enter
```

每一步均有松键报告。

**先在目标应用里手动验证 `Ctrl+Shift+U`，输入 `4f60`，按 Enter 是否得到「你」。** GTK / GNOME 文本编辑器及支持该输入方式的 IBus 环境通常适用；Qt、Electron、浏览器、TTY、BIOS 和部分终端 / 输入法组合可能不支持。单纯改变 HID 固件不能使所有 Linux 应用都接受这一序列，需要在目标系统配置相应输入法支持。本工程不宣称全系统通用中文输入。

若出现 `u4f60`、快捷键被拦截或没有中文，先排查目标应用和输入法；若英文符号不对应，检查键盘布局；若英文字母大小写反转，关闭 Caps Lock。输入法 / 字体不支持某个 emoji 时，设备仍可能成功发送全部按键。

#### macOS

在「系统设置 → 键盘 → 文字输入 → 编辑」添加 **Unicode Hex Input（Unicode 十六进制输入）**，并在目标应用中选中此输入源，再在网页选择「macOS Unicode」。仅选择网页的 macOS 系统并不会替目标设备切换输入源。

```text
你 → 按住 Option → 4f60 → 松开 Option
😀 → 按住 Option → d83dde00 → 松开 Option
```

编码器使用四位 UTF-16 编码单元，补充平面字符使用一对代理项；每次松开数字键时保持 Option，一个码点完成后才松开 Option。请先在目标应用手动验证上述输入方式。普通美式输入源下的 Option 键有其他含义，中文发送前需先切换输入源。

#### Windows

中文输入使用 Windows 的 **Alt + 数字小键盘加号 + 十六进制**方式。先在目标 Windows 的当前用户注册表 `HKEY_CURRENT_USER\Control Panel\Input Method` 下创建字符串值 `EnableHexNumpad=1`，然后注销并重新登录。在 Windows 终端可执行：

```bat
reg add "HKCU\Control Panel\Input Method" /v EnableHexNumpad /t REG_SZ /d 1 /f
```

开启目标设备的 **Num Lock**，在网页选择「Windows Unicode」，先手动验证目标应用：

```text
你 → 按住 Alt → 数字小键盘 + → 4f60 → 松开 Alt
```

固件在整个序列中保持 Alt，十六进制的数字使用数字小键盘键码、a–f 使用字母键码。网页会显示 Num Lock 未开启的提示。

**此模式限定 U+FFFF 以内的字符**，支持常用中文；大多数 emoji 和部分扩展汉字超出此范围，整段请求会在开始输入前拒绝。应用可能拦截 Alt 组合，请先验证目标应用。未启用 EnableHexNumpad 时使用「美式键盘」模式发送英文。

「美式键盘」模式在三套系统下均仅接受 ASCII 可打印字符、换行和 Tab，整段含中文会直接拒绝，不会发送部分内容。macOS / Windows 中途停止会释放 Option / Alt，目标应用可能提交尚未完成的编码；停止不会撤销已经输入的内容。

## 自定义 Wi-Fi

将 `include/config.example.h` 复制为 `include/config.local.h` 后修改，再编译烧录：

```sh
cp include/config.example.h include/config.local.h
```

```cpp
#define HID_AP_PASSWORD "你的热点密码" // 8–63 字节
#define HID_WIFI_SSID "你的2.4GHz Wi-Fi"
#define HID_WIFI_PASSWORD "你的Wi-Fi密码"
```

`config.local.h` 已加入 `.gitignore`。公开的 `config.h` 和 `config.example.h` 只保留通用默认值，不应填写个人 Wi-Fi 凭据。默认热点密码为公开示例值，实际使用时请在 `config.local.h` 设置自己的密码。

SSID 留空时只开启热点；填写后同时连接现有 Wi-Fi，热点仍保留。连到同一局域网后可以尝试 <http://esp32-hid.local>，也可从热点网页查看设备的局域网 IP。mDNS 是否可用取决于网络和客户端环境。

## HTTP API

先 GET `/api/status`，获取每次启动重新生成的 `api_token`。以下所有 POST 均要求 `Content-Type: application/json` 以及请求头 `X-HID-Token: <api_token>`，不启用跨域访问。访问控制依赖热点密码或所连接局域网的访问权限，API token 用于阻止网页跨域提交，不是独立的用户登录机制。

| 接口 | 内容 |
| --- | --- |
| `GET /api/status` | USB 状态、Caps Lock、Num Lock、Wi-Fi IP、发送状态、任务编号、进度和 token |
| `POST /api/type` | 启动一次文字发送，返回 202 |
| `POST /api/key` | 发送一个白名单按键或组合键，返回 202 |
| `POST /api/stop` | 停止当前任务并安排释放按键，返回 200 |

`POST /api/type` 示例：

```json
{
  "text": "你好，Linux！\nHello ESP32-S3",
  "system": "linux",
  "mode": "linux_unicode",
  "interval_ms": 25,
  "start_delay_ms": 0,
  "append_enter": false
}
```

`text` 必填；`system` 可为 `linux`、`macos`、`windows`，省略时为 `linux`。`mode` 可为 `us_ascii` 或对应系统的 `linux_unicode` / `macos_unicode` / `windows_unicode`；系统与 Unicode 模式不匹配时返回 400。省略 `mode` 时，Linux 使用 Linux Unicode，macOS / Windows 使用美式键盘。`interval_ms` 为 10–200 的整数，默认 25，表示相邻按键之间的松开间隔，按下保持约 8 ms；一个中文字符需要多次按键，通常比英文慢。`start_delay_ms` 为 0–10000 的整数，**省略时默认为 0**；`append_enter` 默认 `false`。

`POST /api/key` 示例：`{"key":"PASTE","system":"macos"}` 发送 Command+V。网页的 `SELECT_ALL`、`COPY`、`PASTE`、`UNDO` 按 `system` 选择 Command 或 Ctrl；省略 `system` 时默认 Linux。

固定键码还支持 `ENTER`、`TAB`、`ESC`、`BACKSPACE`、`DELETE`、`UP`、`DOWN`、`LEFT`、`RIGHT`、`HOME`、`END`、`CTRL_A`、`CTRL_C`、`CTRL_V`、`CTRL_Z`、`CMD_A`、`CMD_C`、`CMD_V`、`CMD_Z`、`CTRL_SHIFT_SPACE`、`CTRL_ALT_F1`。这些明确命名的组合键在各系统下保持字面含义；Ctrl+Alt+F1 继续提供网页按钮。组合键行为由目标应用决定，粘贴使用目标设备自身的剪贴板。

状态为 `idle / waiting / sending / done / stopped / error`。一次只接受一个任务，工作中返回 409；USB 未连接或挂起时返回 503；文字 / 参数不合法返回 400；token 不匹配返回 403；请求体超过 32768 字节返回 413；Content-Type 不符合要求返回 415。`completed / total` 统计已发送并松键的 Unicode 码点数量，附加 Enter 也计入，单个快捷键任务计为 1。

**进度表示 USB 报告发送完成，不代表目标应用已确认输入文字。** 网页无法选择或读取目标窗口；输入位置由目标设备当前焦点决定。停止保留已经输入的文字，不会撤回。

## 项目结构

```text
main/
  main.cpp              # Wi-Fi、HTTP API、发送任务
  usb_keyboard.cpp      # TinyUSB 键盘描述符和传输
  usb_keyboard.h
  idf_component.yml     # ESP-IDF 组件依赖
include/
  api_json.h            # JSON 请求解析和参数校验
  hid_text.h            # UTF-8 编码器和发送状态机
  hid_keys.h            # 按键及组合键白名单
  config.h              # 公共默认配置和限制
  config.example.h      # 本地配置模板
web/index.html          # 嵌入固件的离线网页
tests/                  # 原生 C++ 与浏览器交互测试
scripts/test_native.py  # 原生测试入口
sdkconfig.defaults      # 可复现的 SDK 默认配置
partitions.csv          # Flash 分区表
dependencies.lock       # 固定版本和校验值
```

## 实现选择

- 基于 ESP-IDF 6.x，锁定版本为 6.0.3，工程入口为 `app_main()`。根 CMake 工程和 `main/CMakeLists.txt` 负责构建；`sdkconfig.defaults` 保存 ESP32-S3、16 MB Flash、UART 日志、1 kHz FreeRTOS tick 和 HID 默认配置。
- 使用官方托管组件 `esp_tinyusb 2.2.1`、`tinyusb 0.19.0~3`、`mdns 1.14.1`、`cjson 1.7.19~2`，依赖版本和校验值由 `dependencies.lock` 固定。
- `main/usb_keyboard.cpp` 配置原生 USB OTG 和单一标准 Boot Keyboard 接口，不启用 USB CDC。描述符使用 8 字节键盘报告、无 Report ID、2 ms 轮询，供 Linux、macOS 和 Windows 使用；USB 完成回调释放信号量，发送任务只有收到完成通知才计为成功。枚举、挂起 / 恢复、Caps Lock 和 Num Lock 状态通过原子变量与其他任务共享；USB-to-UART 独立输出日志。
- `main/main.cpp` 使用 `esp_wifi` / `esp_netif` 提供 WPA2 Wi-Fi AP 和可选 STA，HTTP 服务使用 `esp_http_server`，发现服务使用 mDNS。热点名称为 `S3-HID-XXXXXX`，后缀取芯片 MAC 后三个字节倒序。网页通过 CMake 直接嵌入 Flash，不依赖 CDN 或互联网。
- `include/hid_text.h` 保留独立于 SDK 的 C++ 编码器和发送状态机，`include/hid_keys.h` 提供包含 Ctrl+Alt+F1 的共享按键白名单。先验证完整 UTF-8 和输入模式，再开始发送；CRLF / CR 统一为一次 Enter。Unicode 逐个码点、逐次按键编码，不构建与文字长度成倍增长的按键队列。Linux 使用 Ctrl+Shift+U 和码点十六进制；macOS 使用 Option 和 UTF-16（含代理对）；Windows 使用 Alt+数字小键盘加号，限定 BMP。每码点最多八次按键，编码步骤明确记录松键后需继续保持的修饰键，仅在码点完成时释放 Option / Alt。
- HTTP 服务与专用 `hid_sender` FreeRTOS 任务独立运行。发送任务优先级为 6，每 1 ms 调度；使用 `esp_timer_get_time()` 计算兼容 32 位回绕的毫秒时间。互斥锁保护发送状态，HTTP 收取请求和发送响应均在锁外，防止慢客户端阻塞松键；每个 USB 报告的完成等待上限为 50 ms。
- `include/api_json.h` 使用 cJSON 解析 JSON，请求体在分配前限制为 32768 字节，文字限制为 4096 个 UTF-8 字节。JSON 深度限制为 8 层；明确拒绝嵌入 U+0000，避免 cJSON 字符串被静默截断。系统在每次文字 / 按键请求中携带，不设置全局目标系统；API 验证系统和输入模式的对应关系。网页仅在 localStorage 保存目标系统和各系统模式，不保存文字。每次启动生成随机 token，POST 要求正确 token 和 JSON Content-Type，不开放 CORS。
- 单任务发送，工作时不排队；停止、报告失败、USB 断开 / 挂起时终止任务，安排全松键，重连不恢复旧文字。中途取消 Linux Unicode 输入还会尝试发送 Escape 退出未完成的 Unicode 组合；macOS / Windows 只释放修饰键，避免向其他系统附加不相关的 Escape 操作。
- 文字只暂存 RAM，任务结束或停止后清空，不写 Flash、不输出文字到串口；设备启动不会自动输入。

## 验证

```sh
python3 scripts/test_native.py
idf.py build
python3 scripts/test_native.py --api
```

原生测试无需开发板，使用系统 C++ 编译器及 AddressSanitizer / UBSan，覆盖严格 UTF-8、三套系统的 Unicode 按键序列、macOS 代理对、ASCII 映射、换行、延时、Option / Alt 的保持和释放、取消、重连不续传、报告失败和毫秒计数溢出。

`--api` 还使用已下载的同一份 cJSON 源码验证 Unicode JSON、U+0000 拒绝、字节上限、参数类型、默认 0 毫秒开始延时、目标系统 / 模式兼容性、Ctrl / Command 编辑快捷键和 Ctrl+Alt+F1 的固定 HID 映射；首次运行前先执行 `idf.py build` 或 `idf.py reconfigure` 获取托管组件。

可选的浏览器交互测试需要 Node.js 和本机 Google Chrome，使用模拟 HTTP API，**不会连接真实开发板**：

```sh
npm install --no-save --package-lock=false --no-audit --no-fund playwright@1.56.1
node tests/test_web.cjs
```

此测试覆盖三套系统的选项和 Unicode 请求内容、默认 0 秒与自定义开始延时、Ctrl / Command 标签、系统和模式偏好的保存、任务期间锁定选项、发送与停止、Num Lock 提示、参数错误、字节上限、断线重连和各系统的 320 / 390 像素手机布局。

硬件验收需在各目标系统验证 `Hello 123 !@#`、常用中文、多行和 Tab；Linux / macOS 再验证 `😀`，Windows 验证超出 BMP 的整段请求被拒绝。配置输入方式后，再检查目标应用中的复制 / 粘贴 / 撤销、延时切焦点、中途停止、拔插 USB 和重复发送被拒绝。自动测试验证发送的报告和网页交互，实际中文显示仍需在目标系统 / 应用实测。

## 常见问题

- **USB 未连接**：检查 USB 数据线、开发板接口和车机 Type-C 接口，确认车机支持外接 USB 键盘。
- **文字输入到错误位置**：固件只向目标设备当前焦点发送按键。同一台电脑既打开网页又接收输入时，请增大开始延时，再切回目标输入框。
- **符号或大小写不对**：使用美式英文键盘布局，关闭 Caps Lock。
- **中文无法输入**：先在目标应用中手动验证对应系统的 Unicode 输入方式；仅在网页切换目标系统不会自动配置目标设备。
- **页面无法打开**：连接设备热点后访问 [192.168.4.1](http://192.168.4.1)，并在手机上选择保持连接到没有互联网的 Wi-Fi。

## 恢复和清理

原生 USB 烧录口消失时，使用 BOOT + RESET 恢复 ROM 下载模式，再执行 `idf.py -p <串口> flash`。`idf.py clean` 清理构建产物，`idf.py fullclean` 清理整个 build 目录；修改网页后重新 `idf.py build` 即可更新嵌入页面。恢复已有固件需要事先保存其源码或固件备份。

`build/`、`managed_components/`、生成的 `sdkconfig`、本地凭据、日志和设备调查资料均不纳入 Git。共享源码时使用已提交的文件；包含本地凭据的固件也可能带有私有信息，不应直接公开。
