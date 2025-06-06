#include "uart_lib.h"
#include <msp430.h>

// Buffer size check - ensures it's a power of 2 for efficient modulo
#if (UART_BUFFER_SIZE & (UART_BUFFER_SIZE - 1)) != 0
    #error UART_BUFFER_SIZE must be a power of 2
#endif

// --- Private Definitions ---

// Circular buffer structure
typedef struct {
    volatile uint8_t buffer[UART_BUFFER_SIZE];
    volatile uint16_t head; // Index of the next free location
    volatile uint16_t tail; // Index of the first item to read
} RingBuffer;

// Static instances of the TX and RX buffers
static RingBuffer rx_buffer;
static RingBuffer tx_buffer;

// --- Function Implementations ---

void uart_init(UartBaudRate baud_rate) {
    // Initialize buffer pointers
    rx_buffer.head = 0;
    rx_buffer.tail = 0;
    tx_buffer.head = 0;
    tx_buffer.tail = 0;

    // Configure P8.2 (RXD) and P8.3 (TXD) for USCI_A1 functionality
    P3DIR |= BIT4 | BIT5;
    P4DIR |= BIT4 | BIT5;
    P4OUT |= BIT4;
    P4OUT &= ~BIT5;
    P3OUT |= BIT5;
    P3OUT &= ~BIT4;
    P8SEL |= BIT2 | BIT3;

    // Place the USCI in reset mode for configuration [cite: 14]
    UCA1CTL1 |= UCSWRST;

    // Configure the clock source. SMCLK is generally preferred over ACLK
    // for higher baud rates and flexibility. [cite: 263]
    UCA1CTL1 |= UCSSEL_2; // Select SMCLK

    // Configure baud rate settings based on a 4MHz SMCLK
    // Values are from the device datasheet, Table 36-4 [cite: 211]
    switch (baud_rate) {
        case BAUD_9600:
            UCA1BR0 = 0xA0;
            UCA1BR1 = 0x01;
            UCA1MCTL = UCBRS_6 | UCBRF_0;
            break;
        case BAUD_19200:
            UCA1BR0 = 208;
            UCA1BR1 = 0;
            UCA1MCTL = UCBRS_3 | UCBRF_0;
            break;
        case BAUD_38400:
            UCA1BR0 = 104;
            UCA1BR1 = 0;
            UCA1MCTL = UCBRS_1 | UCBRF_0;
            break;
        case BAUD_57600:
            UCA1BR0 = 69;
            UCA1BR1 = 0;
            UCA1MCTL = UCBRS_4 | UCBRF_0;
            break;
        case BAUD_115200:
            UCA1BR0 = 34;
            UCA1BR1 = 0;
            UCA1MCTL = UCBRS_6 | UCBRF_0;
            break;
    }

    // Release the USCI for operation [cite: 13]
    UCA1CTL1 &= ~UCSWRST;

    // Enable the RX interrupt. The TX interrupt is only enabled when
    // there is data to send. [cite: 16, 308]
    UCA1IE |= UCRXIE;
}

int uart_write_byte(uint8_t byte) {
    // Calculate next head index
    uint16_t next_head = (tx_buffer.head + 1) & (UART_BUFFER_SIZE - 1);

    // Check if the buffer is full
    if (next_head == tx_buffer.tail) {
        return 0; // Failure, buffer is full
    }

    // Store the byte and update the head
    tx_buffer.buffer[tx_buffer.head] = byte;
    tx_buffer.head = next_head;

    // Enable TX interrupt to start/continue transmission
    UCA1IE |= UCTXIE;

    return 1; // Success
}

uint16_t uart_write_buffer(const uint8_t* buffer, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++) {
        if (!uart_write_byte(buffer[i])) {
            break; // Stop if buffer becomes full
        }
    }
    return i; // Return the number of bytes actually written
}

uint16_t uart_write_uint16_array(const uint16_t* buffer, uint16_t num_samples) {
    // 因为 MSP430 和 PC 都是小端架构，我们可以直接将 uint16_t 数组的指针
    // 转换为 uint8_t 指针。这样做，内存中连续的字节(LSB, MSB, LSB, MSB, ...)
    // 会被 uart_write_buffer 正确地、按顺序地发送出去。
    // 总字节数 = 样本数 * 2。
    return uart_write_buffer((const uint8_t*)buffer, num_samples * 2);
}

int uart_read_byte(uint8_t* byte) {
    // Check if there is data in the buffer
    if (rx_buffer.head == rx_buffer.tail) {
        return 0; // Failure, buffer is empty
    }

    // Atomically read the byte and update the tail
    __disable_interrupt();
    *byte = rx_buffer.buffer[rx_buffer.tail];
    rx_buffer.tail = (rx_buffer.tail + 1) & (UART_BUFFER_SIZE - 1);
    __enable_interrupt();

    return 1; // Success
}

uint16_t uart_available(void) {
    // Calculate the number of bytes in the RX buffer
    return (rx_buffer.head - rx_buffer.tail) & (UART_BUFFER_SIZE - 1);
}

void uart_flush_rx(void) {
    // Atomically reset the buffer pointers
    __disable_interrupt();
    rx_buffer.head = 0;
    rx_buffer.tail = 0;
    __enable_interrupt();
}

// --- Interrupt Service Routine ---

#pragma vector = USCI_A1_VECTOR
__interrupt void USCI_A1_ISR(void) {
    // Using the recommended switch statement for vector generator [cite: 239, 244]
    switch (__even_in_range(UCA1IV, 4)) {
        case 0: // Vector 0: No interrupt
            break;

        case 2: // Vector 2: UCRXIFG - Receive interrupt
        {
            // Calculate next head index
            uint16_t next_head = (rx_buffer.head + 1) & (UART_BUFFER_SIZE - 1);

            // Check if the RX buffer is not full
            if (next_head != rx_buffer.tail) {
                // Read from hardware buffer and store in our software buffer [cite: 285]
                rx_buffer.buffer[rx_buffer.head] = UCA1RXBUF;
                rx_buffer.head = next_head;
            } else {
                // Buffer is full, discard the received byte to prevent overflow
                (void)UCA1RXBUF;
            }
            break;
        }

        case 4: // Vector 4: UCTXIFG - Transmit interrupt
        {
            // Check if there is data to send in the TX buffer
            if (tx_buffer.head != tx_buffer.tail) {
                // Load the next byte into the hardware transmit buffer [cite: 289]
                UCA1TXBUF = tx_buffer.buffer[tx_buffer.tail];
                // Update the tail pointer
                tx_buffer.tail = (tx_buffer.tail + 1) & (UART_BUFFER_SIZE - 1);
            } else {
                // Buffer is empty, disable the transmit interrupt [cite: 228]
                // This is crucial to prevent the ISR from firing continuously
                UCA1IE &= ~UCTXIE;
                UCA1IFG |= UCTXIFG;
            }
            break;
        }
        default:
            break;
    }
}
