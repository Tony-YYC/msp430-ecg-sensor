#include "dr_tft.h"
#include <msp430f6638.h>
#include <stdint.h>
#include <stdio.h>

// Define target MCLK and XT2 crystal frequencies if using the provided init_clock()
// These are example values, adjust them to your hardware.
#define MCLK_FREQ 16000000UL // Example: Target MCLK at 16MHz
#define XT2_FREQ 4000000UL // Example: XT2 crystal at 4MHz

// Buffer to store ADC samples
#define NUM_SAMPLES 300
volatile unsigned int adc_data[NUM_SAMPLES];

// Flag to indicate DMA transfer completion
volatile unsigned char dma_done_flag = 0;

// Function Prototypes
void init_clock(void);
void init_gpio(void);
void init_timer_for_adc(void);
void init_adc(void);
void init_dma_for_adc(void);

void main(void) {
    WDTCTL = WDTPW + WDTHOLD; // Stop watchdog timer

    _DINT();
    initTFT();
    init_clock(); // Initialize clock system
    init_gpio(); // Initialize GPIO (e.g., for ADC input pin function)
    init_timer_for_adc(); // Initialize Timer_A0 to trigger ADC at 200Hz
    init_adc(); // Initialize ADC12_A module
    init_dma_for_adc(); // Initialize DMA Channel 0
    _EINT();
    etft_AreaSet(0, 0, 319, 239, 0);

    __bis_SR_register(GIE); // Enable Global Interrupts

    // ADC and DMA are now running. Timer triggers ADC, ADC triggers DMA.
    // Wait for DMA to complete 500 transfers.
    while (!dma_done_flag) {
        // process the data captured by ADC here.
        etft_DisplayADCVoltage(adc_data, NUM_SAMPLES, (0x3F << 5), 0);

        dma_done_flag = 0; // Clear the flag
        // 1. Re-enable DMA Channel 0 (DMAEN was cleared by hardware)
        DMA0CTL |= DMAEN;

        // 2. Restart Timer_A0 to begin triggering ADC conversions for the next block
        TA0CTL = TASSEL__SMCLK | MC__UP | TACLR; // TACLR ensures it starts fresh
    }

    // DMA transfer of 500 samples is complete.
    // adc_data array now contains the samples.
    // You can now disable ADC/Timer if no more acquisitions are needed.
    TA0CTL = 0; // Stop Timer_A0
    ADC12CTL0 &= ~ADC12ENC; // Disable ADC conversions
    ADC12CTL0 &= ~ADC12ON; // Turn off ADC

    // At this point, adc_data[] is filled with 500 samples.
    // Perform further data processing as needed.
    // For example, toggle an LED to indicate completion.
    P1DIR |= BIT0;
    P1OUT |= BIT0; // Turn on P1.0 LED

    while (1) {
        // Program finished data acquisition and initial processing.
        // Loop indefinitely or perform other tasks.
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
    // P1DIR |= BIT0;
    // P1OUT &= ~BIT0;
}

void init_timer_for_adc(void) {
    // Configure Timer_A0 to trigger ADC at 200Hz (every 5ms)
    // Assuming SMCLK is the source for Timer_A0.
    // SMCLK frequency depends on init_clock(). If SMCLK = XT2 = 4MHz:
    // Timer_period = SMCLK_freq / 200Hz = 4,000,000 / 200 = 20,000 cycles.
    // If SMCLK = DCOCLK = 8MHz:
    // Timer_period = 8,000,000 / 200 = 40,000 cycles. This fits in TA0CCR0.

    TA0CTL = TASSEL__SMCLK | MC__UP | TACLR; // SMCLK, Up mode, Clear TAR
    TA0CCR0 = (MCLK_FREQ / 200)
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
    // ADC12CONSEQx: Conversion sequence mode. 00b for single-channel, single-conversion. [cite: 125, 226]
    // ADC12SSELx: ADC12 clock source select. Example: SMCLK. [cite: 226]
    ADC12CTL1 = ADC12SHP | ADC12SHS_1 | ADC12CONSEQ_0 | ADC12SSEL_3; // SMCLK
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
    __data20_write_long((unsigned long)&DMA0DA, (unsigned long)adc_data);

    // Set DMA Transfer Size (DMA0SZ) [cite: 478, 479]
    // Number of transfers before DMA interrupt
    DMA0SZ = NUM_SAMPLES;

    // Enable DMA Channel 0 [cite: 464]
    DMA0CTL |= DMAEN;
}

// DMA Interrupt Service Routine
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
    #pragma vector = DMA_VECTOR
__interrupt void DMA_ISR(void)
#elif defined(__GNUC__)
void __attribute__((interrupt(DMA_VECTOR))) DMA_ISR(void)
#else
    #error Compiler not supported!
#endif
{
    // DMAIFG for the highest priority enabled DMA channel is automatically cleared
    // by accessing DMAIV if it's not 0. Or manually clear the specific flag.
    // For simplicity, we can check and clear DMA0IFG if needed, but DMAIV is better.
    switch (__even_in_range(DMAIV, 16)) // DMAIV provides the interrupt vector [cite: 482, 484]
    {
        case 0:
            break; // No interrupt
        case 2: // DMA0IFG
            dma_done_flag = 1; // Set flag to indicate completion
            // Add your data processing code here or signal main loop.
            // Keep ISR short. For extensive processing, set a flag and do it in main.
            // Stop Timer_A0 to halt further ADC triggers
            TA0CTL &= MC_0; // Clears MCx bits, effectively stopping the timer
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