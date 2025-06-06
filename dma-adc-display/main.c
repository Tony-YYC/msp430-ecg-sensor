// Define target MCLK and XT2 crystal frequencies if using the provided init_clock()
// These are example values, adjust them to your hardware.
#define MCLK_FREQ 20000000UL // Example: Target MCLK at 20MHz
#define SMCLK_FREQ 4000000UL
#define XT2_FREQ 4000000UL // Example: XT2 crystal at 4MHz

#include "dr_tft.h"
#include "uart_lib.h"
#include <msp430f6638.h>
#include <stdint.h>
#include <stdio.h>

// Buffer to store ADC samples
#define SAMPLES_PER_SEGMENT 40
#define NUM_SEGMENTS 16
#define TOTAL_SAMPLES_ON_SCREEN (SAMPLES_PER_SEGMENT * NUM_SEGMENTS) // 640
unsigned int adc_capture_buffer[TOTAL_SAMPLES_ON_SCREEN];

// Flags/variables to coordinate ISR and main loop
volatile unsigned char segment_data_ready_for_display[NUM_SEGMENTS] = {
    0
}; // ISR sets to 1 when segment_idx data is ready
volatile unsigned int dma_completed_segment_idx =
    0; // ISR sets this to the index of the segment it just finished filling
volatile unsigned char new_dma_data_available = 0; // ISR sets this to 1 on any segment completion

// DMA state - ISR primarily manages this for writing
volatile unsigned int current_segment_dma_is_filling = 0;

// Display state - main loop manages this
unsigned int segment_to_display_next = 0;

// Background color (can be defined or passed)
const uint16_t bRGB_BLACK = 0x0000;
const uint16_t fRGB_GREEN = ((0x3F << 5)); // Pre-calculate if etft_Color is not in main

// Function Prototypes
void init_clock(void);
void init_gpio(void);
void init_timer_for_adc(void);
void init_adc(void);
void init_dma_for_adc(void);
void send_ecg_frame(const uint16_t* data, uint16_t num_samples);

void main(void) {
    WDTCTL = WDTPW + WDTHOLD; // Stop watchdog timer

    _DINT();
    initTFT();
    init_clock(); // Initialize clock system
    init_gpio(); // Initialize GPIO (e.g., for ADC input pin function)
    uart_init(BAUD_115200);
    init_timer_for_adc(); // Initialize Timer_A0 to trigger ADC at 200Hz
    init_adc(); // Initialize ADC12_A module
    init_dma_for_adc(); // Initialize DMA Channel 0
    _EINT();
    etft_AreaSet(0, 0, 319, 239, 0);

    __bis_SR_register(GIE); // Enable Global Interrupts

    while (1) {
        if (new_dma_data_available) {
            new_dma_data_available = 0; // Reset general flag

            // Check if the specific segment we are waiting to display is ready
            if (segment_data_ready_for_display[segment_to_display_next]) {
                segment_data_ready_for_display[segment_to_display_next] =
                    0; // Mark as display processing started

                const uint16_t* p_segment_data =
                    &adc_capture_buffer[segment_to_display_next * SAMPLES_PER_SEGMENT];

                send_ecg_frame(p_segment_data,
                               SAMPLES_PER_SEGMENT); // Send the segment data over UART
                etft_DisplayADCSegment(p_segment_data,
                                       SAMPLES_PER_SEGMENT,
                                       segment_to_display_next, // for screen positioning
                                       NUM_SEGMENTS, // for screen positioning logic
                                       fRGB_GREEN,
                                       bRGB_BLACK);

                // Advance to the next segment to be displayed
                segment_to_display_next = (segment_to_display_next + 1);

                if (segment_to_display_next >= NUM_SEGMENTS) {
                    segment_to_display_next = 0; // Reset for the next display cycle

                    // ---- PAUSE AND CLEAR LOGIC (User Point 2) ----
                    // This occurs after displaying the last segment (NUM_SEGMENTS - 1)
                    // and before the display of segment 0 of the *next* cycle.
                    // ADC/Timer/DMA are still running to capture the next screen's data.
                    // We pause them now to allow a clean screen wipe.

                    TA0CTL = 0; // Stop Timer_A to stop ADC & DMA triggers
                    DMA0CTL &= ~DMAEN; // Disable DMA channel to prevent any pending transfers

                    // Ensure interrupts are off if critical timing for peripheral stop is needed
                    // _DINT(); // Usually not needed if just stopping timer and DMAEN

                    // etft_AreaSet(0,
                    //              0,
                    //              TFT_YSIZE - 1,
                    //              TFT_XSIZE - 1,
                    //              bRGB_BLACK); // Clear entire screen

                    // DMA ISR has already set 'current_segment_dma_is_filling' to 0 and
                    // DMA0DA to '&adc_capture_buffer[0]'.
                    // Reset any stale "ready" flags for display processing just in case.
                    unsigned int k;
                    for (k = 0; k < NUM_SEGMENTS; ++k) {
                        segment_data_ready_for_display[k] = 0;
                    }
                    new_dma_data_available = 0; // Clear this flag as well

                    // DMA0SZ is reloaded automatically.
                    DMA0CTL |= DMAEN; // Re-enable DMA for the new acquisition cycle (segment 0)
                    TA0CTL = TASSEL__SMCLK | MC__UP | TACLR; // Restart Timer_A
                    // _EINT(); // If _DINT() was used
                    // -------------------------------
                }
            }
            // If the specific segment_to_display_next is not yet ready, we'll catch it in the next iteration
            // after new_dma_data_available is set again.
        } else {
            // Optional: Enter Low Power Mode if no flag is set, to save power.
            // __bis_SR_register(LPM0_bits | GIE); // Example: wakes on interrupt (like DMA_ISR)
        }
        P4OUT ^= BIT5; // Toggle LED to show main loop activity
    }
}

void init_clock() {
    // This is the clock initialization from your example.
    // Ensure XT1_FREQ (if used) and XT2_FREQ are defined and match your hardware.
    // For simplicity, if you don't have external crystals, you might configure
    // MCLK and SMCLK from DCO only. This example uses the more complex setup.

    while (
        BAKCTL
        & LOCKIO) // Unlock XT1 pins for operation [Not directly in provided manual but common for F6xxx]
        BAKCTL &= ~(LOCKIO);
    UCSCTL6 &= ~XT1OFF; // Enable XT1 [Not directly in provided manual but common for F6xxx]

    P7SEL |= BIT2 + BIT3; // Select XT2 pins
    UCSCTL6 &= ~XT2OFF; // Enable XT2

    // Wait for XT1, XT2, and DCO to stabilize
    // It's generally better to check individual flags (XT1LFOFFG, XT2OFFG, DCOFFG)
    do {
        UCSCTL7 &= ~(XT2OFFG | XT1LFOFFG | DCOFFG); // Clear DCO, XT1, XT2 fault flags
        SFRIFG1 &= ~OFIFG; // Clear fault interrupt flag
    } while (SFRIFG1 & OFIFG); // Test oscillator fault flag

    // Configure DCO
    // Assuming MCLK_FREQ and XT2_FREQ are defined globally (e.g., 8MHz MCLK, 4MHz XT2)
    UCSCTL4 = SELA__XT1CLK | SELS__XT2CLK
        | SELM__XT2CLK; // Temporarily set MCLK to XT2 to avoid issues during DCO config
    UCSCTL1 = DCORSEL_5; // Select DCO range for MCLK_FREQ (e.g., 6MHz to 23.7MHz for DCORSEL_5)
    UCSCTL2 = (MCLK_FREQ / (XT2_FREQ / 16)) - 1; // FLLN = (MCLK_FREQ / (XT2_FREQ / FLLREFDIV)) - 1
    // Ensure FLLREFDIV below matches.
    // Target DCO = (N+1) * FLLRef
    // N = UCSCTL2, FLLRef = REFCLK / FLLREFDIV
    UCSCTL3 = SELREF__XT2CLK | FLLREFDIV__16; // Set DCO FLL reference = XT2/16

    // Wait for DCO to stabilize again
    do {
        UCSCTL7 &= ~(XT2OFFG | XT1LFOFFG | DCOFFG);
        SFRIFG1 &= ~OFIFG;
    } while (SFRIFG1 & OFIFG);

    UCSCTL5 = DIVA__1 | DIVS__1 | DIVM__1; // Set ACLK, SMCLK, MCLK dividers to 1
    UCSCTL4 = SELA__XT1CLK | SELS__XT2CLK
        | SELM__DCOCLK; // Final clock sources: ACLK=XT1, SMCLK=XT2, MCLK=DCO
}

void init_gpio(void) {
    // Configure ADC input pin
    // For ADC12_A Channel 15 (A15), this is typically P6.7 on MSP430F6638
    // Set P6SEL.7 = 1 for A15 function.
    P6SEL |= BIT7; // Select P6.7 for A15 input
    P6DIR &= ~BIT7; // Set P6.7 as input

    // Example LED (optional, for debugging)
    P4DIR |= BIT5;
    P4REN |= BIT5;
    P4OUT &= ~BIT5;
}

void init_timer_for_adc(void) {
    // Configure Timer_A0 to trigger ADC at 200Hz (every 5ms)
    // Assuming SMCLK is the source for Timer_A0.
    // SMCLK frequency depends on init_clock(). If SMCLK = XT2 = 4MHz:
    // Timer_period = SMCLK_freq / 200Hz = 4,000,000 / 200 = 20,000 cycles.
    // If SMCLK = DCOCLK = 8MHz:
    // Timer_period = 8,000,000 / 200 = 40,000 cycles. This fits in TA0CCR0.

    TA0CTL = TASSEL__SMCLK | MC__UP | TACLR; // SMCLK, Up mode, Clear TAR
    TA0CCR0 = (SMCLK_FREQ / 200)
        - 1; // Period for 200Hz. Using MCLK_FREQ as placeholder, ideally use SMCLK_FREQ.
    // If SMCLK is different from MCLK, adjust this.
    // For example, if SMCLK is XT2_FREQ: TA0CCR0 = (XT2_FREQ / 200) - 1;

    // Configure TA0CCR1 for triggering ADC's Sample-and-Hold input (SHI)
    // We'll use TA0.1 output signal.
    TA0CCTL1 = OUTMOD_3; // Output mode 3: Set/reset.
    // Output (TA0.1) goes high when TAR = TA0CCR1, low when TAR = TA0CCR0.
    // This creates a pulse.
    TA0CCR1 = (TA0CCR0 / 2); // Duty cycle 50% - pulse starts midway.
    // The ADC samples on the rising edge of SHI.
}

void init_adc(void) {
    // Configure ADC12_A module
    // Turn off ADC12ENC to allow configuration [cite: 28]
    ADC12CTL0 &= ~ADC12ENC;

    // ADC12CTL0 configuration
    // ADC12SHT0x: Sample-and-hold time for MEM0-7.
    // Example: ADC12SHT0_8 = 256 ADC12CLK cycles. Adjust if needed. [cite: 218]
    // ADC12ON: ADC12 on. [cite: 218]
    // ADC12MSC: Multiple sample and conversion - SET TO 0 for timer-driven sampling of each point. Default is 0.
    ADC12CTL0 = ADC12SHT0_8 | ADC12ON;

    // ADC12CTL1 configuration
    // ADC12SHP: Sample-and-hold pulse-mode select. SAMPCON is sourced from sampling timer. [cite: 226]
    // ADC12SHSx: Sample-and-hold source select. Select Timer_A0 TA0.1 output. (Value is 1 for TA0.1) [cite: 226]
    // ADC12CONSEQx: Conversion sequence mode. 03b for Repeat-single-channel. [cite: 125, 226]
    // ADC12SSELx: ADC12 clock source select. Example: SMCLK 4MHz. [cite: 226]
    ADC12CTL1 = ADC12SHP | ADC12SHS_1 | ADC12CONSEQ_2 | ADC12SSEL_3; // SMCLK
    // ADC12SHS_1 corresponds to TA0.1

    // ADC12CTL2 configuration (optional, defaults are often fine for basic use)
    // ADC12RES: Resolution. Default is 10b (ADC12RES_1). For 12-bit: ADC12RES_2. [cite: 231]
    ADC12CTL2 = ADC12RES_2; // 12-bit resolution

    // ADC12MCTL0: Conversion memory control for ADC12MEM0
    // ADC12INCHx: Input channel select. Using A15 as per your example. [cite: 242]
    // ADC12SREFx: Voltage reference select. Default (000b) is VR+ = AVCC, VR- = AVSS. [cite: 241]
    ADC12MCTL0 = ADC12INCH_15;

    // Disable ADC12IFG0 interrupt because DMA will use this flag as a trigger [cite: 362]
    ADC12IE &= ~ADC12IFG0; // Clear interrupt enable for ADC12MEM0

    // Enable ADC conversions. The timer will now start triggering conversions. [cite: 222]
    ADC12CTL0 |= ADC12ENC;
}

void init_dma_for_adc(void) {
    // Configure DMA Channel 0
    DMACTL0 &= ~DMA0TSEL_31; // Clear existing trigger selection for Channel 0

    // Select ADC12IFG0 as trigger source for DMA Channel 0 [cite: 439]
    // The specific trigger source number for ADC12 depends on the MSP430 device.
    // For F5xx/F6xx, it's typically 24 (0x18). Check device-specific datasheet or header.
    // Assuming DMA0TSEL__ADC12IFG (or similar) is defined in the header, or use the number.
    // For msp430f6638.h, there might be DMA0TSEL_24 for ADC12IFGx
    DMACTL0 |= DMA0TSEL_24; // Trigger select 24 for ADC12 for Channel 0

    // Configure DMA Channel 0 Control Register (DMA0CTL) [cite: 462]
    DMA0CTL = 0; // Clear existing settings
    DMA0CTL |= DMAIE; // Enable DMA interrupt (triggers when DMA0SZ reaches 0) [cite: 466]
    DMA0CTL |= DMASRCINCR_0; // Source address: No change (ADC12MEM0 is fixed) [cite: 463]
    DMA0CTL |= DMADSTINCR_3; // Destination address: Increment after each transfer [cite: 463]
    DMA0CTL |= DMADT_0; // Transfer mode: Single transfer per trigger [cite: 295, 463]
    // DMA controller will perform DMA0SZ transfers.

    // Set DMA Source Address (DMA0SA) [cite: 469, 471]
    // Needs to be the address of ADC12MEM0
    __data20_write_long((unsigned long)&DMA0SA, (unsigned long)&ADC12MEM0);

    // Set DMA Destination Address (DMA0DA) [cite: 474, 475]
    // Needs to be the start address of our RAM buffer
    __data20_write_long((unsigned long)&DMA0DA, (unsigned long)&adc_capture_buffer[0]);

    // Set DMA Transfer Size (DMA0SZ) [cite: 478, 479]
    // Number of transfers before DMA interrupt
    DMA0SZ = SAMPLES_PER_SEGMENT;

    // Enable DMA Channel 0 [cite: 464]
    DMA0CTL |= DMAEN;
}

// 函数：打包并发送一帧ECG数据
void send_ecg_frame(const uint16_t* data, uint16_t num_samples) {
    uint8_t frame_buffer[2 + 1 + SAMPLES_PER_SEGMENT * 2 + 1];
    uint8_t checksum = 0;
    uint16_t i;
    uint16_t payload_len = num_samples * 2;

    // 1. 填充帧头和长度
    frame_buffer[0] = 0xAA; // 帧头1
    frame_buffer[1] = 0x55; // 帧头2
    frame_buffer[2] = payload_len; // 长度

    // 2. 拷贝数据负载 (因为是小端架构，直接内存拷贝即可)
    memcpy(&frame_buffer[3], data, payload_len);

    // 3. 计算校验和
    for (i = 0; i < payload_len; i++) {
        checksum += frame_buffer[3 + i];
    }

    // 4. 填充校验和
    frame_buffer[3 + payload_len] = checksum;

    // 5. 通过UART库发送整个数据帧
    uart_write_buffer(frame_buffer, sizeof(frame_buffer));
}

#pragma vector = DMA_VECTOR
__interrupt void DMA_ISR(void) {
    // DMAIFG for the highest priority enabled DMA channel is automatically cleared
    // by accessing DMAIV if it's not 0. Or manually clear the specific flag.
    // For simplicity, we can check and clear DMA0IFG if needed, but DMAIV is better.
    switch (__even_in_range(DMAIV, 16)) // DMAIV provides the interrupt vector
    {
        case 0:
            break; // No interrupt
        case 2: // DMA0IFG
            // The segment 'current_segment_dma_is_filling' has just been filled.
            dma_completed_segment_idx = current_segment_dma_is_filling;
            segment_data_ready_for_display[dma_completed_segment_idx] = 1;
            new_dma_data_available = 1;

            // Advance to the next segment for DMA capture
            current_segment_dma_is_filling += 1;
            if (current_segment_dma_is_filling >= NUM_SEGMENTS) {
                current_segment_dma_is_filling = 0;
                // This indicates a full screen's worth of data acquisition has just been set up to start/continue with segment 0.
                // The main loop will handle pausing AFTER displaying the last segment of the previous cycle.
            }

            // Reconfigure DMA destination for the NEW 'current_segment_dma_is_filling'
            unsigned long next_segment_start_addr =
                (unsigned long)&adc_capture_buffer[current_segment_dma_is_filling
                                                   * SAMPLES_PER_SEGMENT];
            __data20_write_long((unsigned long)&DMA0DA, next_segment_start_addr);

            // DMA0SZ is automatically reloaded from its temporary register when it decrements to zero and DMAIFG is set[cite: 41, 53].
            // So, no need to reset DMA0SZ here for DMADT_0.

            // Re-enable DMA Channel 0 (DMAEN was cleared by hardware with DMADT_0 after DMA0SZ transfers) [cite: 34]
            DMA0CTL |= DMAEN;
            break;
        case 4:
            break; // DMA1IFG
        case 6:
            break; // DMA2IFG
        // ... up to 16 for DMA7IFG if available
        default:
            break;
    }
}
