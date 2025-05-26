#include "dr_tft.h"
#include "dr_tft_ascii.h"
#include <msp430.h>

void etft_AreaSet(uint16_t startX, uint16_t startY, uint16_t endX, uint16_t endY, uint16_t color) {
    uint16_t i, j;
    tft_SendCmd(TFTREG_WIN_MINX, startX);
    tft_SendCmd(TFTREG_WIN_MINY, startY);
    tft_SendCmd(TFTREG_WIN_MAXX, endX);
    tft_SendCmd(TFTREG_WIN_MAXY, endY);

    tft_SendCmd(TFTREG_RAM_XADDR, startX);
    tft_SendCmd(TFTREG_RAM_YADDR, startY);

    tft_SendIndex(TFTREG_RAM_ACCESS);
    for (i = 0; i < endY - startY + 1; i++) {
        for (j = 0; j < endX - startX + 1; j++) {
            tft_SendData(color);
        }
    }
}

void etft_DisplayString(const char* str, uint16_t sx, uint16_t sy, uint16_t fRGB, uint16_t bRGB) {
    uint16_t cc = 0;
    uint16_t cx, cy;

    while (1) {
        char curchar = str[cc];
        if (curchar == '\0') //字符串已发送完
            return;

        cx = 0;
        cy = 0;
        //屏幕是横的，XY要对调
        tft_SendCmd(TFTREG_WIN_MINX, sx); //x start point
        tft_SendCmd(TFTREG_WIN_MINY, sy); //y start point
        tft_SendCmd(TFTREG_WIN_MAXX, sx + 7); //x end point
        tft_SendCmd(TFTREG_WIN_MAXY, sy + 15); //y end point
        tft_SendCmd(TFTREG_RAM_XADDR, sx); //x start point
        tft_SendCmd(TFTREG_RAM_YADDR, sy); //y start point
        tft_SendIndex(TFTREG_RAM_ACCESS);

        uint16_t color;
        while (1) {
            if (cx >= 8) {
                cx = 0;
                cy++;
                if (cy >= 16) { //一个字符发送完毕
                    cc++; //下一个字符
                    sx += 8;
                    if (sx >= TFT_YSIZE) //越过行末
                    {
                        sx = 0;
                        sy += 16;
                    }
                    break;
                }
            }

            if ((tft_ascii[curchar * 16 + cy] << cx) & 0x80)
                color = fRGB;
            else
                color = bRGB;

            tft_SendData(color);
            cx++; //X自增
        }
    }
}

void etft_DisplayImage(const uint8_t* image,
                       uint16_t sx,
                       uint16_t sy,
                       uint16_t width,
                       uint16_t height) {
    uint16_t i, j;
    uint32_t row_length = width * 3; //每行像素数乘3
    if (row_length & 0x3) //非4整倍数
    {
        row_length |= 0x03;
        row_length += 1;
    }
    const uint8_t* ptr = image + (height - 1) * row_length;
    tft_SendCmd(TFTREG_WIN_MINX, sx);
    tft_SendCmd(TFTREG_WIN_MINY, sy);
    tft_SendCmd(TFTREG_WIN_MAXX, sx + width - 1);
    tft_SendCmd(TFTREG_WIN_MAXY, sy + height - 1);

    tft_SendCmd(TFTREG_RAM_XADDR, sx);
    tft_SendCmd(TFTREG_RAM_YADDR, sy);

    tft_SendIndex(TFTREG_RAM_ACCESS);
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            tft_SendData(etft_Color(ptr[2], ptr[1], ptr[0]));
            ptr += 3;
        }
        ptr -= width * 3 + row_length;
    }
}

// --- 辅助函数 ---

/**
 * @brief 在指定逻辑坐标绘制一个像素点
 * @param x 逻辑X坐标 (0 到 TFT_YSIZE-1)
 * @param y 逻辑Y坐标 (0 到 TFT_XSIZE-1)
 * @param color 像素颜色
 * @note TFT_YSIZE 是屏幕逻辑宽度 (320), TFT_XSIZE 是屏幕逻辑高度 (240)
 */
static void etft_DrawPixelPriv(uint16_t x, uint16_t y, uint16_t color) {
    // 边界检查
    if (x >= TFT_YSIZE || y >= TFT_XSIZE) {
        return;
    }
    // etft_AreaSet 接收逻辑坐标 (x为横向, y为纵向)
    etft_AreaSet(x, y, x, y, color);
}

/**
 * @brief 使用Bresenham算法绘制一条直线
 * @param x1 起点逻辑X坐标
 * @param y1 起点逻辑Y坐标
 * @param x2 终点逻辑X坐标
 * @param y2 终点逻辑Y坐标
 * @param color 直线颜色
 */
static void etft_DrawLinePriv(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    int16_t dx_abs, dy_abs;
    int16_t sx, sy;
    int16_t err, e2;

    // 计算坐标差的绝对值及符号
    if (x2 > x1) {
        dx_abs = x2 - x1;
        sx = 1;
    } else {
        dx_abs = x1 - x2;
        sx = -1;
    }

    if (y2 > y1) {
        dy_abs = y2 - y1;
        sy = 1;
    } else {
        dy_abs = y1 - y2;
        sy = -1;
    }

    err = dx_abs - dy_abs;

    while (1) {
        etft_DrawPixelPriv(x1, y1, color); // 绘制当前点
        if (x1 == x2 && y1 == y2) { // 如果到达终点则退出
            break;
        }
        e2 = 2 * err;
        if (e2 > -dy_abs) { // e2 大于 -dy_abs
            err -= dy_abs;
            x1 += sx;
        }
        if (e2 < dx_abs) { // e2 小于 dx_abs
            err += dx_abs;
            y1 += sy;
        }
    }
}

// --- 主要绘图函数 ---

/**
 * @brief 在TFT屏幕上显示ADC电压波形
 * @param voltage_array 指向存储ADC采样值的数组 (uint16_t, 0-4095)
 * @param len 数组中的采样点数量 (如果len类型为uint8_t, 则最大为255)
 * @param fRGB 前景色 (波形颜色)
 * @param bRGB 背景色
 */
void etft_DisplayADCVoltage(const uint16_t* voltage_array,
                            uint16_t len,
                            uint16_t fRGB,
                            uint16_t bRGB) {
    // 屏幕逻辑尺寸 (X轴横向, Y轴纵向)
    // 根据 dr_tft.h: TFT_YSIZE 为逻辑宽度 (320), TFT_XSIZE 为逻辑高度 (240)
    const uint16_t screen_width = TFT_YSIZE;
    const uint16_t screen_height = TFT_XSIZE;

    // ADC数值范围 (12-bit ADC)
    const uint16_t adc_max_value = 4095;

    uint16_t i;
    uint16_t x_start_offset;
    uint16_t points_to_display;

    if (len == 0 || voltage_array == 0) {
        return; // 没有数据可以显示
    }

    // 用背景色清空整个屏幕 (或指定的绘图区域)
    // 这里假设波形图占据整个屏幕
    etft_AreaSet(0, 0, screen_width - 1, screen_height - 1, bRGB);

    // 确定实际要显示的采样点数量
    // 由于函数原型中 len 是 uint8_t, 其最大值为255.
    // 屏幕宽度 screen_width (320) 大于255.
    // 因此, points_to_display 将始终等于 len.
    // 如果未来 len 的类型改为 uint16_t 并且可能大于 screen_width,
    // 则下面的注释掉的逻辑将适用.
    if (len > screen_width) {
        points_to_display = screen_width; // 最多显示 screen_width 个点
    } else {
        points_to_display = len;
    }
    // points_to_display = len; // 对于 uint8_t len, 最多显示255个点

    // 计算X轴的起始偏移量，以使波形居中显示
    // 因为 points_to_display (即 len) 总是小于 screen_width
    if (points_to_display < screen_width) {
        x_start_offset = (screen_width - points_to_display) / 2;
    } else {
        x_start_offset = 0; // 如果点数等于或超过屏幕宽度 (此处不会发生，因len为uint8_t)
    }

    uint16_t prev_x_coord = 0;
    uint16_t prev_y_coord = 0;

    // 遍历采样点并绘制
    for (i = 0; i < points_to_display; i++) {
        uint16_t current_adc_value = voltage_array[i];

        //确保ADC值在有效范围内
        if (current_adc_value > adc_max_value) {
            current_adc_value = adc_max_value;
        }

        // 计算当前点在屏幕上的X坐标
        uint16_t current_x_coord = x_start_offset + i;

        // 计算当前点在屏幕上的Y坐标
        // 将ADC值 (0-4095) 映射到屏幕高度 (0 到 screen_height-1)
        // Y=0 是屏幕顶部, Y=screen_height-1 是屏幕底部.
        // ADC值越大, 波形位置越高, 对应的屏幕Y坐标越小.
        // ADC 0   -> Y = screen_height - 1 (底部)
        // ADC 4095 -> Y = 0 (顶部)
        uint32_t temp_y = (uint32_t)current_adc_value
            * (screen_height - 1); // 使用32位整数进行中间乘法运算以防溢出
        uint16_t current_y_coord = (screen_height - 1) - (temp_y / adc_max_value);

        // 理论上 current_y_coord 不会超出边界，但可以加个保险
        if (current_y_coord >= screen_height) {
            current_y_coord = screen_height - 1;
        }

        if (i > 0) {
            // 从上一个点绘制直线到当前点
            etft_DrawLinePriv(prev_x_coord, prev_y_coord, current_x_coord, current_y_coord, fRGB);
        } else {
            // 对于第一个点 (i=0), 我们记录其坐标。
            // 如果只有一个点 (points_to_display == 1), 会在循环末尾单独绘制。
        }

        // 更新上一个点的坐标
        prev_x_coord = current_x_coord;
        prev_y_coord = current_y_coord;

        // 如果总共只有一个点需要显示，则绘制该点
        if (points_to_display == 1 && i == 0) {
            etft_DrawPixelPriv(current_x_coord, current_y_coord, fRGB);
        }
    }
}
