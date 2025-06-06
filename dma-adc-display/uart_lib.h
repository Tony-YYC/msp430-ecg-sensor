#ifndef UART_LIB_H_
#define UART_LIB_H_

#include <stdint.h>

// --- Configuration ---
// Define the size of the circular buffers (must be a power of 2 for efficiency)
#define UART_BUFFER_SIZE 256

// --- Public Types ---
// Enum for common baud rates assuming a 1MHz SMCLK.
// These values are derived from the USCI documentation (Table 36-4)[cite: 211].
typedef enum { BAUD_9600, BAUD_19200, BAUD_38400, BAUD_57600, BAUD_115200 } UartBaudRate;

// --- Public Function Prototypes ---

/**
 * @brief Initializes the USCI_A1 module for UART communication.
 *
 * This function configures the necessary GPIOs, sets the UART registers
 * according to the selected baud rate, and enables the receive interrupt.
 * It follows the initialization procedure outlined in the documentation[cite: 14].
 * @param baud_rate The desired baud rate from the UartBaudRate enum.
 */
void uart_init(UartBaudRate baud_rate);

/**
 * @brief Writes a single byte to the UART transmit buffer.
 *
 * This function is non-blocking. It places the byte in the TX buffer and
 * enables the transmit interrupt to handle the actual sending.
 *
 * @param byte The byte to send.
 * @return 1 on success, 0 if the transmit buffer is full.
 */
int uart_write_byte(uint8_t byte);

/**
 * @brief Writes a block of data to the UART transmit buffer.
 *
 * This function is non-blocking. It copies the data from the provided
 * source buffer into the UART's internal TX buffer.
 *
 * @param buffer Pointer to the data to be sent.
 * @param len The number of bytes to send.
 * @return The number of bytes successfully written to the buffer. This may
 * be less than len if the buffer does not have enough space.
 */
uint16_t uart_write_buffer(const uint8_t* buffer, uint16_t len);

/**
 * @brief Writes an array of uint16_t data to the UART transmit buffer.
 *
 * This function handles sending 16-bit data by breaking each element
 * into two bytes (LSB first, due to little-endian architecture).
 *
 * @param buffer Pointer to the uint16_t data array to be sent.
 * @param num_samples The number of uint16_t samples to send.
 * @return The number of bytes successfully written to the buffer.
 */
uint16_t uart_write_uint16_array(const uint16_t* buffer, uint16_t num_samples);

/**
 * @brief Reads a single byte from the UART receive buffer.
 *
 * @param byte Pointer to a variable where the read byte will be stored.
 * @return 1 on success (a byte was read), 0 if the receive buffer is empty.
 */
int uart_read_byte(uint8_t* byte);

/**
 * @brief Returns the number of bytes available in the receive buffer.
 *
 * @return The number of unread bytes in the RX buffer.
 */
uint16_t uart_available(void);

/**
 * @brief Clears the UART receive buffer.
 *
 * This function discards any unread data in the RX buffer.
 */
void uart_flush_rx(void);

#endif /* UART_LIB_H_ */
