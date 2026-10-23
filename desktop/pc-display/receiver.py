"""Manual local receiver: USB only, no server, disk recording or remote input."""
import argparse
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk
import serial
from serial.tools import list_ports
from protocol import (Packet, Parser, FRAME, STATUS, END, HELLO, READY, ACK, STOP,
                      pack, decode, ppm)


class Connection:
    def __init__(self, port):
        self.port = port
        self.stop = threading.Event()
        self.messages = queue.Queue(maxsize=4)
        self.frames = queue.Queue(maxsize=1)
        self.acks = queue.Queue(maxsize=1)
        self.thread = threading.Thread(target=self.run, name='Yumo USB receiver', daemon=True)

    def message(self, text):
        while True:
            try:
                self.messages.put_nowait(text)
                return
            except queue.Full:
                try:
                    self.messages.get_nowait()
                except queue.Empty:
                    pass

    def acknowledge(self, sid, seq):
        try:
            self.acks.put_nowait((sid, seq))
        except queue.Full:
            pass

    def run(self):
        device = None
        sid = 0
        try:
            # Do not assert ESP32 boot/reset lines when opening or closing the port.
            device = serial.Serial(port=None, baudrate=1000000, timeout=0.02, write_timeout=0.2)
            device.dtr = False
            device.rts = False
            device.port = self.port
            device.open()
            device.reset_input_buffer() # Discard bytes from a previously closed session.
            parser = Parser()
            last_receive = last_hello = last_ack = time.monotonic()
            last_displayed = 0
            device.write(pack(Packet(HELLO)))
            self.message('等待设备：设置 → 显示与声音 → 开启 USB 画面投放')
            while not self.stop.is_set():
                now = time.monotonic()
                if not sid and now - last_hello >= 1:
                    device.write(pack(Packet(HELLO)))
                    last_hello = now
                try:
                    ack_sid, ack_seq = self.acks.get_nowait()
                    if ack_sid == sid:
                        last_displayed = ack_seq
                        device.write(pack(Packet(ACK, sid, ack_seq)))
                        last_ack = now
                except queue.Empty:
                    pass
                if sid and now - last_ack >= 0.5:
                    device.write(pack(Packet(ACK, sid, last_displayed)))
                    last_ack = now
                data = device.read(min(8192, max(1, device.in_waiting)))
                for packet in parser.feed(data):
                    if packet.kind == STATUS and packet.session:
                        if sid and packet.session != sid:
                            raise ValueError('设备投放会话已改变，请重新连接')
                        if not sid:
                            sid = packet.session
                            device.write(pack(Packet(READY, sid)))
                            self.message('已连接，正在接收首帧…')
                    if packet.session != sid or not sid:
                        continue
                    last_receive = time.monotonic()
                    if packet.kind == END or not packet.flags:
                        self.message('投放已结束；再次使用请在设备上手动开启')
                        return
                    if packet.kind == FRAME:
                        pixels = decode(packet)
                        # Exactly one complete frame; GUI acknowledgement drives pacing.
                        try:
                            self.frames.put_nowait((sid, packet.sequence, ppm(pixels), len(data)))
                        except queue.Full:
                            pass
                if sid and time.monotonic() - last_receive > 2.5:
                    raise TimeoutError('设备无响应，投放已断开')
        except (serial.SerialException, OSError, ValueError, TimeoutError) as exc:
            self.message('连接失败：' + str(exc))
        finally:
            if device is not None:
                try:
                    if device.is_open and sid:
                        device.write(pack(Packet(STOP, sid, flags=0)))
                except (serial.SerialException, OSError):
                    pass
                device.close()


class Viewer:
    def __init__(self, root):
        self.root, self.connection = root, None
        self.image = self.base_image = None
        self.closed = False
        self.frame_count = 0
        self.stats_time = time.monotonic()
        root.title('雨墨 · USB 画面投放')
        root.geometry('760x760')
        root.minsize(530, 430)
        root.configure(bg='#11151c')
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('TFrame', background='#11151c')
        style.configure('TLabel', background='#11151c', foreground='#dce5ef')
        style.configure('TButton', padding=(12, 8))
        bar = ttk.Frame(root, padding=10)
        bar.pack(fill='x')
        self.port = ttk.Combobox(bar, state='readonly', width=27)
        self.port.pack(side='left', fill='x', expand=True, padx=(0, 8))
        self.refresh_button = ttk.Button(bar, text='刷新', command=self.refresh)
        self.refresh_button.pack(side='left', padx=4)
        self.connect_button = ttk.Button(bar, text='连接', command=self.toggle)
        self.connect_button.pack(side='left', padx=4)
        ttk.Button(bar, text='全屏', command=self.fullscreen).pack(side='left')
        self.canvas = tk.Canvas(root, bg='black', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, padx=10)
        self.canvas.bind('<Configure>', lambda _: self.redraw())
        self.status = tk.StringVar(value='用 USB 数据线连接设备，手动开启设备投放开关，再点击连接。')
        ttk.Label(root, textvariable=self.status, wraplength=720, padding=10).pack(fill='x')
        ttk.Label(root, text='直接操作设备按键、触屏或蓝牙键盘。F11 全屏 / Esc 返回；声音仍由设备播放。',
                  wraplength=720, padding=(10, 0, 10, 10)).pack(fill='x')
        root.bind('<F11>', self.fullscreen)
        root.bind('<Escape>', lambda _: root.attributes('-fullscreen', False))
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.refresh()
        root.after(20, self.poll)

    def refresh(self):
        self.ports = list(list_ports.comports())
        self.port['values'] = [f'{p.device}  {p.description}' for p in self.ports]
        if self.ports:
            preferred = next((i for i, p in enumerate(self.ports) if p.vid == 0x303A and p.pid == 0x1001), 0)
            self.port.current(preferred)

    def toggle(self):
        if self.connection:
            self.connection.stop.set()
            self.connect_button['state'] = 'disabled'
            self.clear()
            self.status.set('正在断开…')
            return
        index = self.port.current()
        if index < 0 or index >= len(self.ports):
            self.status.set('没有可用设备，请检查 USB 数据线后刷新。')
            return
        self.connection = Connection(self.ports[index].device)
        self.connection.thread.start()
        self.port['state'] = self.refresh_button['state'] = 'disabled'
        self.connect_button['text'] = '断开'
        self.stats_time, self.frame_count = time.monotonic(), 0

    def clear(self):
        self.canvas.delete('all')
        self.base_image = self.image = None

    def redraw(self):
        if self.base_image is None:
            return
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        scale = max(1, min(8, width // 240, height // 240))
        self.image = self.base_image.zoom(scale, scale) if scale > 1 else self.base_image
        self.canvas.delete('all')
        self.canvas.create_image(width // 2, height // 2, image=self.image)

    def poll(self):
        connection = self.connection
        if connection:
            while not connection.messages.empty():
                try:
                    self.status.set(connection.messages.get_nowait())
                except queue.Empty:
                    break
            if connection.thread.is_alive() and not connection.stop.is_set():
                try:
                    sid, seq, picture, _ = connection.frames.get_nowait()
                    self.base_image = tk.PhotoImage(data=picture, format='PPM')
                    self.redraw()
                    self.root.after_idle(connection.acknowledge, sid, seq)
                    self.frame_count += 1
                    now = time.monotonic()
                    if self.frame_count == 1:
                        self.status.set('正在投放 · 240×240 · 本机背光已关闭')
                    elif now - self.stats_time >= 2:
                        fps = self.frame_count / (now - self.stats_time)
                        self.status.set(f'正在投放 · 240×240 · {fps:.1f} 帧/秒（静止画面不重复发送）')
                        self.stats_time, self.frame_count = now, 0
                except queue.Empty:
                    pass
                except tk.TclError:
                    connection.stop.set()
                    self.status.set('画面显示失败，已停止投放')
            if not connection.thread.is_alive():
                self.clear()
                self.connection = None
                self.port['state'] = 'readonly'
                self.refresh_button['state'] = self.connect_button['state'] = 'normal'
                self.connect_button['text'] = '连接'
        if self.closed:
            if self.connection is None:
                self.root.destroy()
                return
        self.root.after(20, self.poll)

    def fullscreen(self, _=None):
        self.root.attributes('-fullscreen', not self.root.attributes('-fullscreen'))

    def close(self):
        self.closed = True
        self.clear()
        if self.connection:
            self.connection.stop.set()
        self.root.withdraw()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smoke-test', action='store_true', help='test Tk display without opening a device')
    args = parser.parse_args()
    root = tk.Tk()
    viewer = Viewer(root)
    if args.smoke_test:
        root.withdraw()
        test = b'\x00\xf8' * (240 * 240)
        viewer.base_image = tk.PhotoImage(data=ppm(test), format='PPM')
        assert viewer.base_image.get(0, 0) == (255, 0, 0)
        root.update_idletasks()
        viewer.redraw()
        root.destroy()
        print('GUI smoke test passed; no serial port opened')
        return
    root.mainloop()


if __name__ == '__main__':
    main()
