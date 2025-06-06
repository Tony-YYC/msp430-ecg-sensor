import serial
import struct
import threading
import collections
import time
import numpy as np
from matplotlib import pyplot as plt
from matplotlib.animation import FuncAnimation
from scipy.signal import find_peaks

plt.rcParams['font.sans-serif'] = ['SimHei'] # Or any other Chinese font you have
plt.rcParams['axes.unicode_minus'] = False # Display minus sign correctly

# --- 1. 配置区更新 ---
# 串口配置
SERIAL_PORT = '/dev/ttyACM0'  # !!! 重要：修改为你的MSP430连接的COM端口
BAUD_RATE = 9600

# 帧格式定义
FRAME_HEADER = b'\xAA\x55'
DATA_SAMPLES_PER_FRAME = 40
DATA_BYTES_PER_FRAME = DATA_SAMPLES_PER_FRAME * 2

# ADC与采样配置 (新增)
SAMPLE_RATE = 200  # 采样率 (Hz)
V_REF = 3.3        # ADC参考电压 (V)
ADC_RESOLUTION = 4095 # 12-bit ADC -> 2^12 - 1

# 绘图与分析配置 (新增与修改)
DISPLAY_SECONDS = 5.0 # 在屏幕上显示5秒的数据
PLOT_SAMPLES = int(DISPLAY_SECONDS * SAMPLE_RATE) # 总共显示1000个点

# 心率计算配置 (新增)
HR_PEAK_THRESHOLD_V = 1.5  # R波峰值检测的电压阈值 (V), !!! 重要：这个值需要根据你的实际信号幅度进行调整
HR_MIN_PEAK_DISTANCE_SAMPLES = int(SAMPLE_RATE * 0.3) # 两个R波之间的最小采样点数 (0.3秒对应最高200BPM)

# --- 全局变量 ---
# 修改deque长度以缓存5秒的数据
data_queue = collections.deque(maxlen=PLOT_SAMPLES)
exit_flag = False
last_heart_rate = 0 # 用于在数据不足时显示上一次的心率

def calculate_checksum(payload):
    """计算8位累加和校验"""
    return sum(payload) & 0xFF

def calculate_heart_rate(voltage_data, sample_rate):
    """
    使用scipy.signal.find_peaks计算心率
    返回: (计算出的心率BPM, R波峰值的索引列表)
    """
    if len(voltage_data) < HR_MIN_PEAK_DISTANCE_SAMPLES:
        return 0, []

    # 使用find_peaks寻找R波
    # height: 峰值的最小高度 (阈值)
    # distance: 相邻峰之间的最小水平距离 (采样点数)
    peaks, _ = find_peaks(voltage_data, height=HR_PEAK_THRESHOLD_V, distance=HR_MIN_PEAK_DISTANCE_SAMPLES)

    if len(peaks) < 2:
        # 峰值数量不足，无法计算心率
        return 0, peaks

    # 计算R-R间隔 (单位：秒)
    rr_intervals = np.diff(peaks) / sample_rate

    if rr_intervals.size == 0:
        return 0, peaks

    # 计算平均心率 (BPM)
    avg_rr_interval = np.mean(rr_intervals)
    heart_rate_bpm = 60.0 / avg_rr_interval

    return heart_rate_bpm, peaks


def parse_serial_data(ser):
    """运行在独立线程中，负责接收和解析串口数据 (功能不变)"""
    STATE_WAIT_HEADER = 0
    STATE_READ_LENGTH = 1
    STATE_READ_PAYLOAD = 2
    STATE_READ_CHECKSUM = 3

    state = STATE_WAIT_HEADER
    payload_buffer = bytearray()
    
    print("数据接收线程已启动...")
    while not exit_flag:
        try:
            # ... (这部分的状态机逻辑与之前的版本完全相同，此处省略以保持简洁) ...
            if state == STATE_WAIT_HEADER:
                if ser.read(1) == FRAME_HEADER[0:1] and ser.read(1) == FRAME_HEADER[1:2]:
                    state = STATE_READ_LENGTH
            elif state == STATE_READ_LENGTH:
                length_byte = ser.read(1)
                if length_byte and int.from_bytes(length_byte, 'little') == DATA_BYTES_PER_FRAME:
                    state = STATE_READ_PAYLOAD
                    payload_buffer.clear()
                else:
                    state = STATE_WAIT_HEADER
            elif state == STATE_READ_PAYLOAD:
                payload_buffer.extend(ser.read(DATA_BYTES_PER_FRAME - len(payload_buffer)))
                if len(payload_buffer) == DATA_BYTES_PER_FRAME:
                    state = STATE_READ_CHECKSUM
            elif state == STATE_READ_CHECKSUM:
                received_checksum = ser.read(1)
                if received_checksum and int.from_bytes(received_checksum, 'little') == calculate_checksum(payload_buffer):
                    unpacked_data = struct.unpack(f'<{DATA_SAMPLES_PER_FRAME}H', payload_buffer)
                    data_queue.extend(unpacked_data)
                    print(f"成功接收一帧数据，校验通过。样本[0]: {unpacked_data[0]}")
                else:
                    print(f"错误：校验和不匹配！")
                state = STATE_WAIT_HEADER
        except Exception as e:
            print(f"串口读取或解析时发生错误: {e}")
            state = STATE_WAIT_HEADER # 出错后重置状态机
            time.sleep(1)


# --- 2. 绘图函数重大更新 ---
fig, ax = plt.subplots(figsize=(12, 4))
line, = ax.plot([], [], lw=1.5, color='b') # 波形线
peak_dots, = ax.plot([], [], 'ro') # R波峰值点
# MODIFICATION 1: 在这里声明文本对象变量
hr_text = None

def init_plot():
    """初始化绘图区域"""
    global hr_text # 声明使用全局变量
    ax.set_xlim(0, DISPLAY_SECONDS) # X轴显示时间
    ax.set_ylim(-0.5, V_REF + 0.5) # Y轴显示电压，留出一些边距
    ax.set_title('实时心电信号 (ECG)')
    ax.set_xlabel('时间 (s)')
    ax.set_ylabel('电压 (V)')
    ax.grid(True)

    # MODIFICATION 2: 创建文本框对象
    # transform=ax.transAxes 表示坐标是相对于坐标轴的百分比
    # (0.02, 0.95) 表示在左上角
    hr_text = ax.text(0.02, 0.95, '', transform=ax.transAxes, fontsize=12,
                      verticalalignment='top', bbox=dict(boxstyle='round,pad=0.5', fc='yellow', alpha=0.5))

    return line, peak_dots, hr_text,

def update_plot(frame):
    """更新绘图数据和心率"""
    global last_heart_rate
    
    # 将队列中的原始ADC数据转换为numpy数组
    raw_data_array = np.array(data_queue)
    
    if len(raw_data_array) == 0:
        return line, peak_dots,

    # 1. Y轴: ADC值转换为电压
    voltage_array = (raw_data_array / ADC_RESOLUTION) * V_REF
    
    # 2. X轴: 采样点索引转换为时间
    time_array = np.arange(len(voltage_array)) * (1.0 / SAMPLE_RATE)
    
    # 3. 心率计算
    heart_rate, peak_indices = calculate_heart_rate(voltage_array, SAMPLE_RATE)
    
    if heart_rate > 0:
        last_heart_rate = heart_rate
    
    # 更新波形图
    line.set_data(time_array, voltage_array)
    
    # 更新R波峰值标记
    if len(peak_indices) > 0:
        peak_times = peak_indices * (1.0 / SAMPLE_RATE)
        peak_voltages = voltage_array[peak_indices]
        peak_dots.set_data(peak_times, peak_voltages)
    else:
        peak_dots.set_data([],[])
        
    # MODIFICATION 4: 更新文本框的内容，而不是标题
    hr_text.set_text(f'心率: {last_heart_rate:.0f} BPM')
    
    # 动态调整Y轴范围以便更好地观察信号
    if len(voltage_array) > 10:
        min_v = np.min(voltage_array) - 0.2
        max_v = np.max(voltage_array) + 0.2
        ax.set_ylim(min_v, max_v)
        
    return line, peak_dots, hr_text,

# --- 主程序 ---
if __name__ == '__main__':
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"已打开串口 {SERIAL_PORT}，波特率 {BAUD_RATE}")
    except serial.SerialException as e:
        print(f"无法打开串口 {SERIAL_PORT}: {e}")
        exit()

    serial_thread = threading.Thread(target=parse_serial_data, args=(ser,))
    serial_thread.daemon = True # 设置为守护线程，主程序退出时它也退出
    serial_thread.start()

    ani = FuncAnimation(fig, update_plot, init_func=init_plot, blit=True, interval=100, cache_frame_data=False)

    plt.show()

    exit_flag = True
    print("正在关闭程序...")
    # 线程是守护线程，会自动退出
    ser.close()
    print("程序已退出。")