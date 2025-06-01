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
 * @brief Displays a single segment of the ADC voltage waveform using averaging.
 * @param segment_data_ptr Pointer to the start of the current segment's ADC data.
 * @param samples_in_segment Number of ADC samples in this segment (e.g., 40).
 * @param segment_idx_for_positioning The index of the current segment (0 to NUM_SEGMENTS-1) for X positioning.
 * @param num_total_segments_on_screen Total number of segments the screen is divided into (e.g., 16).
 * @param fRGB Foreground color for the waveform.
 * @param bRGB Background color for this segment's area.
 */
void etft_DisplayADCSegment(const uint16_t* segment_data_ptr,
                            uint16_t samples_in_segment,
                            uint16_t segment_idx_for_positioning,
                            uint16_t num_total_segments_on_screen,
                            uint16_t fRGB,
                            uint16_t bRGB) {
    const uint16_t screen_total_width = TFT_YSIZE; // 320 (logical width)
    const uint16_t screen_height = TFT_XSIZE; // 240 (logical height)
    const uint16_t adc_max_value = 4095;

    if (samples_in_segment == 0 || segment_data_ptr == 0 || num_total_segments_on_screen == 0) {
        return;
    }

    uint16_t segment_pixel_width =
        screen_total_width / num_total_segments_on_screen; // e.g., 320 / 16 = 20 pixels
    if (segment_pixel_width == 0)
        segment_pixel_width = 1; // Prevent division by zero

    uint16_t x_start_on_screen_for_segment = segment_idx_for_positioning * segment_pixel_width;

    uint16_t prev_x_coord_on_screen = 0; // These will be screen absolute coordinates
    uint16_t prev_y_coord_on_screen = 0;

    // Downsampling: 'samples_in_segment' (e.g., 40) to 'segment_pixel_width' (e.g., 20) columns.
    // Each pixel column will represent an average of (samples_in_segment / segment_pixel_width) samples.
    uint16_t samples_to_average_per_pixel = 1;
    if (segment_pixel_width > 0 && samples_in_segment > segment_pixel_width) {
        samples_to_average_per_pixel =
            samples_in_segment / segment_pixel_width; // e.g., 40 / 20 = 2
    }

    uint16_t i, k;
    for (i = 0; i < segment_pixel_width; i++)
    { // 'i' is the pixel column index (0 to 19 for a 20px segment)
        uint32_t sum_of_samples = 0;
        uint16_t num_actual_samples_averaged = 0;
        uint16_t start_sample_idx_in_segment = i * samples_to_average_per_pixel;

        for (k = 0; k < samples_to_average_per_pixel; k++) {
            uint16_t current_sample_idx = start_sample_idx_in_segment + k;
            if (current_sample_idx < samples_in_segment) {
                sum_of_samples += segment_data_ptr[current_sample_idx];
                num_actual_samples_averaged++;
            } else {
                break; // No more samples in the segment data for this pixel column
            }
        }

        uint16_t averaged_adc_value;
        if (num_actual_samples_averaged > 0) {
            averaged_adc_value = sum_of_samples / num_actual_samples_averaged;
        } else {
            // Fallback: if no samples could be averaged (e.g., if segment_pixel_width > samples_in_segment)
            // take the first available sample for this pixel column if possible.
            if (start_sample_idx_in_segment < samples_in_segment) {
                averaged_adc_value = segment_data_ptr[start_sample_idx_in_segment];
            } else {
                averaged_adc_value = 0; // Or some default/last known good value
            }
        }

        if (averaged_adc_value > adc_max_value)
            averaged_adc_value = adc_max_value; // Clamp

        uint16_t current_x_coord_on_screen = x_start_on_screen_for_segment + i;

        uint32_t temp_y = (uint32_t)averaged_adc_value * (screen_height - 1);
        uint16_t current_y_coord_on_screen = (screen_height - 1) - (temp_y / adc_max_value);
        if (current_y_coord_on_screen >= screen_height)
            current_y_coord_on_screen = screen_height - 1;

        if (i > 0) { // If not the first point in this segment's drawing loop
            etft_DrawLinePriv(prev_x_coord_on_screen,
                              prev_y_coord_on_screen,
                              current_x_coord_on_screen,
                              current_y_coord_on_screen,
                              fRGB);
        } else {
            etft_DrawPixelPriv(current_x_coord_on_screen, current_y_coord_on_screen, fRGB);
        }
        prev_x_coord_on_screen = current_x_coord_on_screen;
        prev_y_coord_on_screen = current_y_coord_on_screen;
    }
}
