import serial
import struct
import time
import collections
import threading
from matplotlib import pyplot as plt
from matplotlib.animation import FuncAnimation
import numpy as np

# --- 配置区 ---
SERIAL_PORT = 'COM3'  # !!! 重要：修改为你的MSP430连接的COM端口
BAUD_RATE = 115200

# 帧格式定义
FRAME_HEADER = b'\xAA\x55'
DATA_SAMPLES = 40  # 40个 uint16_t 样本
DATA_BYTES = DATA_SAMPLES * 2  # 负载字节数

# 绘图配置
PLOT_SAMPLES = 200 # 窗口中显示最新200个采样点

# --- 全局变量 ---
# 使用deque实现一个固定长度的队列，用于存储待绘图的数据
data_queue = collections.deque(maxlen=PLOT_SAMPLES)
# 线程控制标志
exit_flag = False

def calculate_checksum(payload):
    """计算8位累加和校验"""
    return sum(payload) & 0xFF

def parse_serial_data(ser):
    """
    运行在独立线程中，负责接收和解析串口数据。
    使用状态机来解析数据帧。
    """
    STATE_WAIT_HEADER = 0
    STATE_READ_LENGTH = 1
    STATE_READ_PAYLOAD = 2
    STATE_READ_CHECKSUM = 3

    state = STATE_WAIT_HEADER
    payload_buffer = bytearray()
    
    print("数据接收线程已启动...")
    while not exit_flag:
        try:
            if state == STATE_WAIT_HEADER:
                # 等待并匹配两个字节的帧头
                if ser.read(1) == FRAME_HEADER[0:1]:
                    if ser.read(1) == FRAME_HEADER[1:2]:
                        state = STATE_READ_LENGTH
            
            elif state == STATE_READ_LENGTH:
                # 读取长度字节
                length_byte = ser.read(1)
                if length_byte and int.from_bytes(length_byte, 'little') == DATA_BYTES:
                    state = STATE_READ_PAYLOAD
                    payload_buffer.clear()
                else:
                    # 长度不匹配，回到初始状态
                    print(f"错误：长度字节不匹配！期望 {DATA_BYTES}, 收到 {length_byte}")
                    state = STATE_WAIT_HEADER

            elif state == STATE_READ_PAYLOAD:
                # 读取80个字节的数据负载
                bytes_to_read = DATA_BYTES - len(payload_buffer)
                payload_buffer.extend(ser.read(bytes_to_read))

                if len(payload_buffer) == DATA_BYTES:
                    state = STATE_READ_CHECKSUM

            elif state == STATE_READ_CHECKSUM:
                received_checksum = ser.read(1)
                if received_checksum:
                    calculated_checksum = calculate_checksum(payload_buffer)
                    if int.from_bytes(received_checksum, 'little') == calculated_checksum:
                        # 校验成功，解包数据
                        # '<40H' 表示 小端(little-endian), 40个, 无符号短整型(unsigned short)
                        unpacked_data = struct.unpack(f'<{DATA_SAMPLES}H', payload_buffer)
                        data_queue.extend(unpacked_data)
                        print(f"成功接收一帧数据，校验通过。样本[0]: {unpacked_data[0]}")
                    else:
                        print(f"错误：校验和不匹配！收到: {int.from_bytes(received_checksum, 'little')}, 计算: {calculated_checksum}")
                
                # 无论成功与否，都回到初始状态，准备接收下一帧
                state = STATE_WAIT_HEADER

        except serial.SerialException as e:
            print(f"串口错误: {e}")
            break
        except Exception as e:
            print(f"发生错误: {e}")
            state = STATE_WAIT_HEADER # 出错后重置状态机

# --- 绘图函数 ---
fig, ax = plt.subplots()
line, = ax.plot([], [])

def init_plot():
    """初始化绘图区域"""
    ax.set_xlim(0, PLOT_SAMPLES)
    ax.set_ylim(0, 4096) # 12-bit ADC -> 0-4095
    ax.set_title('实时心电信号 (ECG)')
    ax.set_xlabel('采样点')
    ax.set_ylabel('ADC读数')
    ax.grid(True)
    return line,

def update_plot(frame):
    """更新绘图数据"""
    # 将deque转换为numpy数组以提高绘图性能
    plot_data = np.array(data_queue)
    line.set_data(np.arange(len(plot_data)), plot_data)
    return line,

# --- 主程序 ---
if __name__ == '__main__':
    try:
        # 打开串口
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"已打开串口 {SERIAL_PORT}，波特率 {BAUD_RATE}")
    except serial.SerialException as e:
        print(f"无法打开串口 {SERIAL_PORT}: {e}")
        exit()

    # 创建并启动数据接收线程
    serial_thread = threading.Thread(target=parse_serial_data, args=(ser,))
    serial_thread.start()

    # 设置并启动Matplotlib动态绘图
    # interval设为50ms刷新一次，可以根据需要调整
    ani = FuncAnimation(fig, update_plot, init_func=init_plot, blit=True, interval=50, cache_frame_data=False)

    plt.show()

    # 绘图窗口关闭后，设置退出标志，关闭线程和串口
    print("正在关闭程序...")
    exit_flag = True
    serial_thread.join()
    ser.close()
    print("程序已退出。")