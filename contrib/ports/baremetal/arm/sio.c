#include <lwip/sio.h>
#include "ringbuffer.c"
#include "stm32l1xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "uart.h"

typedef struct sio_uart {
    UART_HandleTypeDef handle;
    ring_buffer_t buffer;
    uint8_t recvByte;
} sio_uart_t;

static sio_uart_t uart_list[5];

/**
  * @brief  This function handles modem UART interrupt request.
  * @param  None
  * @retval None
  */
void USART1_IRQHandler(void);
void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart_list[UART_DEVNUM_DEBUG].handle);
}

/**
  * @brief  This function handles modem UART interrupt request.
  * @param  None
  * @retval None
  */
void USART2_IRQHandler(void);
void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart_list[UART_DEVNUM_MODEM].handle);
}

static void HAL_UART_EnableInterrupt(sio_uart_t *uart)
{
  HAL_UART_Receive_IT(&(uart->handle), (uint8_t *)&uart->recvByte, 1);
}

static void HAL_UART_HandleInterrupt(sio_uart_t *uart)
{
  ring_buffer_queue(&(uart->buffer), uart->recvByte);
  HAL_UART_EnableInterrupt(uart);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *UartHandle)
{
  switch ((int)UartHandle->Instance)
  {
    default:
      return;
    case USART1_BASE:
    {
      HAL_UART_HandleInterrupt(&(uart_list[UART_DEVNUM_DEBUG]));
      return;
    }
    case USART2_BASE:
    {
      HAL_UART_HandleInterrupt(&(uart_list[UART_DEVNUM_MODEM]));
      return;
    }
  }
}

static sio_uart_t * debug_uart_init(int uart_num, uint32_t baud_rate)
{

  sio_uart_t *uart = uart_list + uart_num;
  uart->handle.Instance = USART1;

  uart->handle.Init.BaudRate = baud_rate;
  uart->handle.Init.WordLength = UART_WORDLENGTH_8B;
  uart->handle.Init.StopBits = UART_STOPBITS_1;
  uart->handle.Init.Parity = UART_PARITY_NONE;
  uart->handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  uart->handle.Init.Mode = UART_MODE_TX_RX;
  ring_buffer_init(&(uart->buffer));

  if (HAL_UART_Init(&uart->handle) != HAL_OK)
  {
    return NULL;
  }

  HAL_UART_EnableInterrupt(uart);

  return uart;
}

static sio_uart_t * modem_uart_init(int uart_num, uint32_t baud_rate)
{
  sio_uart_t *uart = uart_list + uart_num;

  uart->handle.Instance = USART2;

  uart->handle.Init.BaudRate = baud_rate;
  uart->handle.Init.WordLength = UART_WORDLENGTH_8B;
  uart->handle.Init.StopBits = UART_STOPBITS_1;
  uart->handle.Init.Parity = UART_PARITY_NONE;
  uart->handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  uart->handle.Init.Mode = UART_MODE_TX_RX;
  ring_buffer_init(&(uart->buffer));

  if (HAL_UART_Init(&uart->handle) != HAL_OK)
  {
    return NULL;
  }

  HAL_UART_EnableInterrupt(uart);
  return uart;
}

/* The following functions can be defined to something else in your cc.h file
   or be implemented in your custom sio.c file. */

/**
 * Opens a serial device for communication.
 *
 * @param devnum device number
 * @return handle to serial device if successful, NULL otherwise
 */
sio_fd_t sio_open(u8_t devnum, u32_t baud_rate)
{
  switch (devnum)
  {
    default:
      return NULL;
    case UART_DEVNUM_DEBUG:
      return (sio_fd_t)debug_uart_init(devnum, baud_rate);
    case UART_DEVNUM_MODEM:
      return (sio_fd_t)modem_uart_init(devnum, baud_rate);
  }
  return NULL;
}

static int sio_write_byte(sio_fd_t fd, uint8_t ch, int32_t try_count)
{
    int sent = 0;
    for (;try_count > 0; try_count -= 1)
    {
      if (HAL_UART_Transmit(fd, &ch, 1, 0xFFFF) == HAL_OK)
      {
        sent = 1;
        break;
      }
    }
    return sent;
}

/**
 * Sends a single character to the serial device.
 *
 * @param c character to send
 * @param fd serial device handle
 *
 * @note This function will block until the character can be sent.
 */
void sio_send(u8_t c, sio_fd_t fd)
{
  sio_write_byte(fd, c, 128);
}

/**
 * Receives a single character from the serial device.
 *
 * @param fd serial device handle
 *
 * @note This function will block until a character is received.
 */
u8_t sio_recv(sio_fd_t fd)
{
    return 0;
}

/**
 * Reads from the serial device.
 *
 * @param fd serial device handle
 * @param data pointer to data buffer for receiving
 * @param len maximum length (in bytes) of data to receive
 * @return number of bytes actually received - may be 0 if aborted by sio_read_abort
 *
 * @note This function will block until data can be received. The blocking
 * can be cancelled by calling sio_read_abort().
 */
u32_t sio_read(sio_fd_t fd, u8_t *data, u32_t len)
{
  return 0;
}

/**
 * Tries to read from the serial device. Same as sio_read but returns
 * immediately if no data is available and never blocks.
 *
 * @param fd serial device handle
 * @param data pointer to data buffer for receiving
 * @param len maximum length (in bytes) of data to receive
 * @return number of bytes actually received
 */
u32_t sio_tryread(sio_fd_t fd, u8_t *data, u32_t len)
{
  sio_uart_t *uart = (sio_uart_t *)fd;
  HAL_UART_EnableInterrupt(fd);

  uint16_t offset = 0;
  while (ring_buffer_dequeue(&(uart->buffer), (char*)(data + offset)) > 0)
  {
    offset += 1;
    if (offset == len - 1)
    {
      break;
    }
  }
  data[offset] = 0;
  return offset;
}

/**
 * Writes to the serial device.
 *
 * @param fd serial device handle
 * @param data pointer to data to send
 * @param len length (in bytes) of data to send
 * @return number of bytes actually sent
 *
 * @note This function will block until all data can be sent.
 */
u32_t sio_write(sio_fd_t fd, const u8_t *data, u32_t len)
{
  u32_t i;
  for (i = 0; i < len; i += 1)
  {
    /* Send failed, then return the number of bytes already sent */
    if (!sio_write_byte(fd, data[i], 16))
    {
      return i;
    }
  }
  return len;
}

/**
 * Aborts a blocking sio_read() call.
 *
 * @param fd serial device handle
 */
void sio_read_abort(sio_fd_t fd)
{

}

u8_t sio_reconnected(sio_fd_t fd)
{
  return 0;
}
