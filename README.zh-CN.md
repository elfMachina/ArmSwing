# ArmSwing

**让 VRChat 全身追踪走路时手臂自然摆动。**

[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | 简体中文 | [Español](README.es.md) | [Português (BR)](README.pt-BR.md) | [Русский](README.ru.md)

在 VRChat 中使用全身追踪并用摇杆行走时，身体像在滑行，手臂却僵硬不动。
ArmSwing 会在你行走时把垂下的手臂交给 VRChat 自带的行走动画，让手臂自然摆动。
抬起手，或按住握把或扳机，那只手臂会在同一帧重新跟随你的控制器。

## 系统要求

- Windows 10（1809 或更高版本）或 Windows 11，以及 SteamVR
- VRChat（Steam 版），并且**已完成全身追踪校准**：追踪器、SlimeVR、Standable，
  或任何 VRChat 能按全身追踪校准的方案
- 带摇杆的控制器（见“支持的控制器”）
- **开启 VRChat 的 OSC**：动作菜单（Action Menu）> Options > OSC > Enabled

没有全身追踪（三点追踪）时，ArmSwing 会让手臂保持正常追踪。三点追踪的支持正在
开发中。

## 安装

1. 关闭 SteamVR。
2. 将整个 zip 解压到一个固定的文件夹，例如 `C:\Tools\ArmSwing`。
   你应该能看到 `armswing` 文件夹、`install.bat`、`uninstall.bat` 和 README 文件。
3. 双击 `install.bat`。
4. 启动 SteamVR，然后运行 `armswing\armswing.exe`。

不会打开任何窗口：ArmSwing 在系统托盘（时钟旁边）运行。右键单击图标即可退出。
关闭 SteamVR 时它会自动退出。

**更新：** 关闭 SteamVR，解压新的 zip，并运行其中的 `install.bat`。它会自动注销
旧版本的注册。然后删除旧文件夹。

**如果 Windows 发出警告：** ArmSwing 目前还没有代码签名，所以运行
`install.bat` 或 `armswing.exe` 时，Windows SmartScreen 可能会显示
“Windows 已保护你的电脑”。请点击**更多信息**，然后点击**仍要运行**。由于驱动
会像其他 SteamVR 驱动插件一样挂接到 SteamVR，部分杀毒软件也可能报毒。全部源码
都是公开的，每个版本都会附上 SHA-256 校验值，你可以用它确认下载的文件是原版。

## 使用方法

- 手臂垂下时用左摇杆行走：手臂会摆动。
- 把手臂抬到大约腰部以上，或按住该手的握把或扳机：即使在行走中，那只手臂也会
  立即重新跟随控制器。
- 当你推着摇杆但 VRChat 实际上没有让你移动时（朝墙走、相机飞行模式、坐着、
  蹲得很低），手臂会保持追踪。
- ArmSwing 运行期间会在所有 SteamVR 游戏中生效。玩其他游戏前，请从托盘图标退出它。

## 支持的控制器

无需设置即可使用：PlayStation VR2 Sense、Meta Quest 和 Rift Touch（Quest Link、
Air Link、Steam Link、Virtual Desktop、ALVR）、Valve Index、Steam Frame、
Vive Cosmos、HP Reverb G2、Windows Mixed Reality。Pico 和 Vive Focus 3 / XR Elite
的控制器（通过 Steam Link）会自动使用 Quest 的预设。

其他带摇杆的控制器：在 SteamVR > 设置 > 控制器 > 管理控制器绑定 > ArmSwing 中，
把你用于移动的摇杆绑定到 `left_stick_pos`。不支持 Vive 手柄（它是触控板，不是
摇杆）。

## 检查你的设备环境

在下表中找到你的情况。表中提到的设置项在“设置”一节中有说明。

| 你的情况 | 该怎么做 |
|---|---|
| 上面列表中的控制器 | 什么都不用做。 |
| 其他带摇杆的控制器 | 绑定 `left_stick_pos`（见上文）。 |
| 可以松手的控制器（Globular Cluster 等绑带、放松的 Index 绑带） | 可选：设为 `wake_mode=touch`，手碰着控制器时那只手臂就保持追踪。 |
| 手臂放下时不摆动 | 把 `raise_drop` 调小（例如 `0.50`）。 |
| 想保持追踪的手臂却在摆动 | 把 `raise_drop` 调大（例如 `0.90`）。 |
| 没有全身追踪（三点追踪） | 暂不支持：手臂会照常保持追踪。 |
| 你用右摇杆移动 | 暂不支持。 |
| 非 Steam 版 VRChat，或 VRChat 的 OSC 未开启 | 只要推动摇杆，手臂就会摆动，对着墙或在菜单中也一样。想要完整效果，请使用 Steam 版并开启 OSC。 |
| 安装文件夹的路径含特殊字符（表情符号，或与 Windows 语言不同的文字） | 请安装到简单的路径，例如 `C:\Tools\ArmSwing`。 |

## 设置

用文本编辑器编辑 `armswing\config.txt`。可以在 VR 中直接保存，大约 1 秒内生效。

| 设置 | 默认值 | 作用 |
|---|---|---|
| `raise_drop` | `0.70` | 头显下方多少米以上算作“手臂已抬起”。数值越小，需要把手臂抬得越高才能保持追踪。 |
| `wake_mode` | `grip` | `grip`：按住握把或扳机时，手臂在任何高度都保持追踪。`touch`：触摸传感器也算在内；只适用于可以松手的控制器（例如带绑带的）。 |
| `wake_hold` | `120` | 松开握把或扳机后，手臂继续保持追踪的时间（毫秒）。 |
| `walk_hold` | `90` | 推动摇杆多久后手臂才开始摆动（毫秒）。用于忽略摇杆的轻微抖动。 |

如果想在不卸载的情况下关闭 ArmSwing，在 `armswing\mode.txt` 中写入 `off`
（写入 `9` 即可重新开启）。

## 故障排除

把鼠标悬停在托盘图标上可以看到状态。日志在 `armswing` 文件夹中：
`armswing-watcher.log`（托盘程序）和 `armswing.log`（SteamVR 驱动）。上一次的
日志会保留为 `*.prev`。

- **对着墙、在菜单中或相机模式下手臂在摆动：** VRChat 的 OSC 未开启。请开启
  （Action Menu > Options > OSC > Enabled）。正常工作时，`armswing-watcher.log`
  中会出现 `VRChat's OSC arriving`。
- **手臂完全不摆动：** 在 `armswing-watcher.log` 中查找
  `[stick] action INACTIVE`。这表示没有与你的控制器匹配的绑定（见“支持的
  控制器”）。同时确认已在 VRChat 中完成全身追踪校准。
- **只有一只手臂不摆动：** 你可能按着那只手的握把或扳机，或者在手持的控制器上
  使用了 `wake_mode=touch`。
- **其他问题：** 请提交 issue，并附上 `armswing-watcher.log`、`armswing.log`
  以及它们的 `.prev` 文件。

## 兼容性与隐私

- **其他 OSC 应用**（VRCFaceTracking 等）可以照常使用：ArmSwing 从不使用 9000
  和 9001 端口。VRChat 通过 OSCQuery 找到 ArmSwing，并发送到它自己的端口。
- **其他 SteamVR 插件**可以照常使用：ArmSwing 只改变行走时两只手是否显示为
  “已追踪”，不添加任何设备。
- **网络：** ArmSwing 只在 127.0.0.1 上监听，电脑以外的设备无法连接。为了让
  VRChat 找到它，它会像 VRChat 和其他 OSC 应用一样，通过 mDNS 在本地网络上
  广播自己。它从不连接互联网，也不收集任何数据。

## 卸载

关闭 SteamVR，运行 `uninstall.bat`，然后删除文件夹。

## 从源码构建

请参阅[英文 README](README.md#building-from-source)。
