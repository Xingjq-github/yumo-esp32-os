# USB 电脑投屏

自行安装带 Tkinter 的 Python 3.11，并在该 Python 环境执行：

```bat
python -m pip install -r desktop/pc-display/requirements.txt
python desktop/pc-display/receiver.py
```

Windows 也可双击 `desktop/pc-display/Start-Display.cmd`，启动器使用 PATH 中的 Python。

在设备“设置 → 显示与声音”中手动打开 USB 投屏，电脑客户端选择对应串口并连接。“USB投屏时熄屏”由独立开关控制，默认不关闭设备背光。停止投屏后释放接收端串口，烧录前关闭接收窗口。

本功能通过 USB 将画面传到电脑程序，不支持普通 USB-C HDMI 扩展坞直连显示器。USB 投屏与 SD 读卡器互斥，不依赖密码本服务。详见 [协议与实现](../firmware/usb_display.md)。
