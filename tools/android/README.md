# shySOFT Host 安卓（BLE）

与 Windows 上位机同一套 JSON CLI：监视 / 配置 / 自检 / 射频 / IO / 原始命令。  
传输走片内 GATT，**不是**经典蓝牙 SPP。

| 项 | 值 |
|----|-----|
| 广播名 | `MBA01-xxxx` |
| Service | `0xFEE7` |
| Write + Notify | `0xFEC1` |
| 格式 | 一行 JSON + `\n`，**无** NS_BlueTooth 2 字节长度头 |

板端必须 **插着 USB** 才开蓝牙。应答只回手机这一路。

## 用 Android Studio 编译

本机仓库里没有 JDK/SDK，需要你本机的 Android Studio。

1. 安装 [Android Studio](https://developer.android.com/studio)（Koala / Ladybug 均可），首次打开让它装 SDK（API 34）和 JDK 17。
2. **File → Open** 选本目录：`tools/android`
3. 若提示缺少 Gradle Wrapper，点 OK 让 IDE 生成；或 Sync 后用 IDE 自带 Gradle。
4. 连手机（打开开发者选项 / USB 调试），点绿色 Run。  
   也可 **Build → Build APK(s)**，产物在 `app/build/outputs/apk/debug/`。

命令行（已装 SDK 时）：

```bat
cd tools\android
echo sdk.dir=C:\Users\你的用户名\AppData\Local\Android\Sdk> local.properties
gradlew.bat assembleDebug
```

若还没有 `gradlew.bat`：用 Android Studio 打开工程后会自动生成。

## 联调

1. 烧**新固件**（广播改为完整名 0x09、间隔约 100ms），**插入 USB**。
2. 手机打开 **系统定位**（国产机不打开，BLE 扫描结果经常是空的）。
3. App 允许「附近设备」+「定位」→ **扫描**。状态栏会显示「已听 N 个 命中 M」。
4. 点 `MBA01-xxxx` 连接，再 **Ping**。

电脑 nRF Connect 能看到、旧 App 看不到：多半是安卓没解析缩短名 0x08，或定位没开。新固件 + 新 App 一起刷。

拔 USB 会关蓝牙，App 会显示断开。

## 权限

Android 12+：附近设备（`BLUETOOTH_SCAN` / `CONNECT`）。  
Android 11 及以下：定位（系统扫 BLE 的要求）。

## 目录

```text
tools/android/
├── app/src/main/java/com/xinghai/mba01/
│   ├── MainActivity.kt
│   ├── AppViewModel.kt
│   ├── ble/BleClient.kt      # 扫、连、FEC1 分包写 / 拼行
│   ├── proto/Cli.kt
│   ├── radio/RadioParser.kt  # 与 host_pc 同一套 GSV/GGA/BDPWI
│   └── ui/HostApp.kt
└── README.md
```
