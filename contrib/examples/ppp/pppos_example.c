/*
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Dirk Ziegelmeier <dziegel@gmx.de>
 *
 */


#include "pppos_example.h"

#if PPPOS_SUPPORT
#include "lwip/dns.h"
#include "lwip/log.h"
#include "netif/ppp/pppos.h"
#include "netif/ppp/pppdebug.h"
#include "lwipcfg.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

enum at_result_enum {
    at_result_failure_no_response = -1,
    at_result_success = 0,
    at_result_failure_error = 1,
};

/*
Switch from Data Mode to Command Mode

Use Sequence +++ to Switch from Data Mode to Command Mode

The other way to switch USB/UART port from data mode to command mode is using sequence +++ when
PPP connection has been set up successfully. To prevent the +++ escape sequence from being
misinterpreted as data, the following sequence should be followed:
1) Do not input any character within 1s or longer before inputting "+++".
2) Input "+++" within 1s, and no other characters can be inputted during the time;
3) Do not input any character within 1s after "+++" has been inputted.
When such particular sequence +++ is received, the USB/UART port will switch from data mode to
command mode, and the module will return OK for the operation.
*/
enum PPPOS_ChatscriptState {
  PPPOS_CHATSCRIPT_BEGIN,
  PPPOS_CHATSCRIPT_AT_MODE_PRE_WAIT,
  PPPOS_CHATSCRIPT_SEND_PLUS_PLUS_PLUS,
  PPPOS_CHATSCRIPT_AT_MODE_POST_WAIT,
  PPPOS_CHATSCRIPT_START_ATE0,
  PPPOS_CHATSCRIPT_START_CFUN,
  PPPOS_CHATSCRIPT_START_CGDCONT,
  PPPOS_CHATSCRIPT_START_CGDCONT_QUERY,
  PPPOS_CHATSCRIPT_START_CREG_QUERY,
  PPPOS_CHATSCRIPT_START_CGDATA,
  PPPOS_CHATSCRIPT_ON_CONNECT
};

typedef enum at_result_enum (* pppos_command_result_check)(const char *at_result);

typedef struct pppos_command {
  sys_timeout_handler callback;
  const char *cmd;
  const char *expect_result;
  pppos_command_result_check check;
  const char *expect_error;
  uint32_t timeout;
  uint32_t start_time;
  int8_t try_count;

  enum at_result_enum at_result;
} pppos_command_t;

typedef struct pppos_modem
{
  sio_fd_t sio;
  ppp_pcb *ppp;
  struct netif netif;
  u8_t phase_saved;

  pppos_command_t command;

  int8_t find_result_pos;
  enum PPPOS_ChatscriptState state;
  enum PPPOS_ChatscriptState success_state;
  enum PPPOS_ChatscriptState failure_state;
  u8_t *sio_buffer;
  uint32_t sio_buffer_size;

  char command_ack[128]; /* The command ack result */
  u16_t command_ack_len;

  char buffer_int[128]; /* The command ack buffer for parsing ack result */
  int buffer_int_pos;

  uint8_t buffer_cmd_ack_pos;
  uint8_t buffer_cmd_ack_count;
  char buffer_cmd_ack[2][128]; /* Storage multiple ack result */
} pppos_modem_t;

static void pppos_input_connect(pppos_modem_t *modem, u8_t *s, int l);
static void pppos_state_interval(void *arg);
static void pppos_modem_receive_reset(pppos_modem_t *modem);

static void
pppos_rx_input(ppp_pcb *pcb, const void *buffer, int len)
{
#if !NO_SYS && !PPP_INPROC_IRQ_SAFE
  /* Pass received raw characters from PPPoS to be decoded through lwIP
  * TCPIP thread using the TCPIP API. This is thread safe in all cases
  * but you should avoid passing data byte after byte. */
  pppos_input_tcpip(pcb, buffer, len);
#else
  pppos_input(pcb, buffer, len);
#endif
}

static void
pppos_rx_interval(void *arg)
{
  pppos_modem_t *modem = (pppos_modem_t *)arg;
  for (;;) {
    int len = sio_tryread(modem->sio, modem->sio_buffer, modem->sio_buffer_size);
    if (len > 0) {
      pppos_input_connect(modem, modem->sio_buffer, len);
    } else {
      break;
    }
  }
  sys_timeout(1, pppos_rx_interval, arg);
}

static void
pppos_modem_start(pppos_modem_t *modem)
{
  pppos_modem_receive_reset(modem);
  modem->state = PPPOS_CHATSCRIPT_START_ATE0;
  modem->phase_saved = 0xff;
  sys_timeout(1, pppos_rx_interval, (void *)modem);
  sys_timeout(1, pppos_state_interval, (void *)modem);
}

static void
ppp_link_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
  struct netif *pppif = ppp_netif(pcb);
  LWIP_UNUSED_ARG(ctx);

  switch (err_code) {
  case PPPERR_NONE: /* No error. */
  {
#if LWIP_DNS
    const ip_addr_t *ns;
#endif /* LWIP_DNS */
    lwip_printfw("ppp_link_status_cb: PPPERR_NONE\n\r");
#if LWIP_IPV4
    lwip_printfw("   our_ip4addr = %s\n\r", ip4addr_ntoa(netif_ip4_addr(pppif)));
    lwip_printfw("   his_ipaddr  = %s\n\r", ip4addr_ntoa(netif_ip4_gw(pppif)));
    lwip_printfw("   netmask     = %s\n\r", ip4addr_ntoa(netif_ip4_netmask(pppif)));
#endif /* LWIP_IPV4 */
#if LWIP_IPV6
    lwip_printfw("   our_ip6addr = %s\n\r", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
#endif /* LWIP_IPV6 */

#if LWIP_DNS
    ns = dns_getserver(0);
    lwip_printfw("   dns1        = %s\n\r", ipaddr_ntoa(ns));
    ns = dns_getserver(1);
    lwip_printfw("   dns2        = %s\n\r", ipaddr_ntoa(ns));
#endif /* LWIP_DNS */
#if PPP_IPV6_SUPPORT
    lwip_printfw("   our6_ipaddr = %s\n\r", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
#endif /* PPP_IPV6_SUPPORT */
  } break;

  case PPPERR_PARAM: /* Invalid parameter. */
    lwip_printfi("ppp_link_status_cb: PPPERR_PARAM\n");
    break;

  case PPPERR_OPEN: /* Unable to open PPP session. */
    lwip_printfi("ppp_link_status_cb: PPPERR_OPEN\n");
    break;

  case PPPERR_DEVICE: /* Invalid I/O device for PPP. */
    lwip_printfi("ppp_link_status_cb: PPPERR_DEVICE\n");
    break;

  case PPPERR_ALLOC: /* Unable to allocate resources. */
    lwip_printfi("ppp_link_status_cb: PPPERR_ALLOC\n");
    break;

  case PPPERR_USER: /* User interrupt. */
    lwip_printfi("ppp_link_status_cb: PPPERR_USER\n");
    break;

  case PPPERR_CONNECT: /* Connection lost. */
    lwip_printfi("ppp_link_status_cb: PPPERR_CONNECT\n");
    break;

  case PPPERR_AUTHFAIL: /* Failed authentication challenge. */
    lwip_printfi("ppp_link_status_cb: PPPERR_AUTHFAIL\n");
    break;

  case PPPERR_PROTOCOL: /* Failed to meet protocol. */
    lwip_printfi("ppp_link_status_cb: PPPERR_PROTOCOL\n");
    break;

  case PPPERR_PEERDEAD: /* Connection timeout. */
    lwip_printfi("ppp_link_status_cb: PPPERR_PEERDEAD\n");
    break;

  case PPPERR_IDLETIMEOUT: /* Idle Timeout. */
    lwip_printfi("ppp_link_status_cb: PPPERR_IDLETIMEOUT\n");
    break;

  case PPPERR_CONNECTTIME: /* PPPERR_CONNECTTIME. */
    lwip_printfi("ppp_link_status_cb: PPPERR_CONNECTTIME\n");
    break;

  case PPPERR_LOOPBACK: /* Connection timeout. */
    lwip_printfi("ppp_link_status_cb: PPPERR_LOOPBACK\n");
    break;

  default:
    lwip_printfi("ppp_link_status_cb: unknown errCode %d\n", err_code);
    break;
  }
}

static u32_t
ppp_output_cb(ppp_pcb *pcb, const void *data, u32_t len, void *ctx)
{
  pppos_modem_t *modem = (pppos_modem_t *)ctx;
  LWIP_UNUSED_ARG(pcb);
  if (modem->state == PPPOS_CHATSCRIPT_ON_CONNECT) {
    return sio_write(modem->sio, (const u8_t *)data, len);
  }
  return 0;
}

#if LWIP_NETIF_STATUS_CALLBACK
static void
netif_status_callback(struct netif *nif)
{
  lwip_printfi("PPPNETIF: %c%c%d is %s\n", nif->name[0], nif->name[1], nif->num,
         netif_is_up(nif) ? "UP" : "DOWN");
#if LWIP_IPV4
  lwip_printfi("IPV4: Host at %s ", ip4addr_ntoa(netif_ip4_addr(nif)));
  lwip_printfi("mask %s ", ip4addr_ntoa(netif_ip4_netmask(nif)));
  lwip_printfi("gateway %s\n", ip4addr_ntoa(netif_ip4_gw(nif)));
#endif /* LWIP_IPV4 */
#if LWIP_IPV6
  lwip_printfi("IPV6: Host at %s\n", ip6addr_ntoa(netif_ip6_addr(nif, 0)));
#endif /* LWIP_IPV6 */
#if LWIP_NETIF_HOSTNAME
  lwip_printfi("FQDN: %s\n", netif_get_hostname(nif));
#endif /* LWIP_NETIF_HOSTNAME */
}
#endif /* LWIP_NETIF_STATUS_CALLBACK */
#endif

/* 0x0D 0x0A, (截断) 0x0D 0x0A */
const uint32_t line_splitter = 0x0A << 16 | 0x0D << 8 | 0x0A;

const uint32_t line_end = 0x0D << 8 | 0x0A;

static void pppos_byte_process(pppos_modem_t *modem, char data)
{
  modem->buffer_int[modem->buffer_int_pos] = data;
  modem->buffer_int_pos += 1;
  // The last byte storage 0
  if ((modem->command_ack_len + 1)< sizeof(modem->command_ack)) {
    modem->command_ack[modem->command_ack_len] = data;
    ++modem->command_ack_len;
  }
  if (modem->buffer_int_pos >= 3) {
    const uint32_t val = modem->buffer_int[modem->buffer_int_pos - 3] << 16 |
                         modem->buffer_int[modem->buffer_int_pos - 2] << 8 |
                         modem->buffer_int[modem->buffer_int_pos - 1] << 0;
    /* 异常情况 查找 0x0A, (截断) 0x0D 0x0A */
    if (line_splitter == val) {
      modem->buffer_int_pos = 2;
      modem->buffer_int[0] = 0x0D;
      modem->buffer_int[1] = 0x0A;
    } else if ((val & 0xFFFF) == line_end) {
      modem->buffer_int[modem->buffer_int_pos] = 0;
      {
        /* receive at cmd ack */
        memcpy(modem->buffer_cmd_ack[modem->buffer_cmd_ack_pos % 2], modem->buffer_int, modem->buffer_int_pos + 1);
        modem->buffer_cmd_ack_pos += 1;
        modem->buffer_cmd_ack_count += 1;
      }

      PPPDEBUG(LOG_DEBUG, ("MODEN->MCU: %s\n", modem->buffer_int));

      modem->buffer_int_pos = 0;
    }
  }
}

static void pppos_input_connect(pppos_modem_t *modem, u8_t *s, int l)
{
  if (modem->state != PPPOS_CHATSCRIPT_ON_CONNECT) {
    int i = 0;
    for (i = 0; i < l; i += 1) {
      pppos_byte_process(modem, s[i]);
    }
  } else {
    pppos_rx_input(modem->ppp, s, l);
  }
}

static void pppos_modem_receive_reset(pppos_modem_t *modem)
{
  modem->buffer_int_pos = 0;
  modem->buffer_cmd_ack_count = 0;
  modem->buffer_cmd_ack_pos = 0;
}

/*
以 \n 分割成 N 个字符串
当所有字符串都可以在 任意一个 modemBuffer.buffer_cmd_ack[i] 中找到才成功
*/
static int find_cmd_ack(pppos_modem_t *modem, const char *str)
{
  char str_to_find[100];
  const char *pos = str;
  int find_pos = -1;
  int ack_count = modem->buffer_cmd_ack_count < 2 ? modem->buffer_cmd_ack_count : 2;
  if (!str) {
    return find_pos;
  }

  while (pos != NULL) {
    int find_substr;
    int i;
    const char *new_pos = strstr(pos, "\n");
    if (new_pos == NULL) {
      strcpy(str_to_find, pos);
      pos = NULL;
    } else {
      memcpy(str_to_find, pos, new_pos - pos);
      str_to_find[new_pos - pos] = 0;
      pos = new_pos + 1;
    }

    find_substr = -1;
    for (i = 0; i < ack_count; i += 1) {
      if (strstr(modem->buffer_cmd_ack[i], str_to_find) != NULL) {
        find_pos = i;
        find_substr = 0;
      }
    }
    /* 如果当前这个字符串没找到，那就直接失败 */
    if (find_substr == -1) {
      find_pos = -1;
      break;
    }
  }

  return find_pos;
}

static enum at_result_enum pppos_find_ack(pppos_modem_t *modem, const char *result, const char *error)
{
  modem->find_result_pos = find_cmd_ack(modem, result);
  if (modem->find_result_pos >= 0) {
    return at_result_success;
  }
  if (find_cmd_ack(modem, error) >= 0) {
    return at_result_failure_error;
  }
  return at_result_failure_no_response;
}

static void pppos_command_execute(void *arg);
static void pppos_command_wait(void *arg)
{
  pppos_modem_t *modem = (pppos_modem_t *)arg;
  modem->command.at_result = pppos_find_ack(modem, modem->command.expect_result, modem->command.expect_error);
  if (
    modem->command.at_result == at_result_success ||
    modem->command.at_result == at_result_failure_error
  ) {
    modem->command.try_count = 0;
    sys_timeout(0, pppos_command_execute, (void *)modem);
  } else if (modem->command.start_time + modem->command.timeout < sys_now()) {
    modem->command.try_count -= 1;
    sys_timeout(0, pppos_command_execute, (void *)modem);
  } else {
    sys_timeout(10, pppos_command_wait, (void *)modem);
  }
}

static void pppos_command_execute(void *arg)
{
  pppos_modem_t *modem = (pppos_modem_t *)arg;
  if (modem->command.try_count <= 0) {
    if (modem->command.check)
    {
      modem->command.at_result = modem->command.check(modem->command_ack);
    }

    if (modem->command.at_result == at_result_success) {
      modem->state = modem->success_state;
    } else {
      modem->state = modem->failure_state;
    }
    sys_timeout(0, modem->command.callback, modem);
  } else {
    pppos_modem_receive_reset(modem);
    PPPDEBUG(LOG_DEBUG, ("execute %s\n", modem->command.cmd));
    sio_write(modem->sio, (const u8_t *)modem->command.cmd, (u32_t)strlen(modem->command.cmd));
    modem->command.start_time = sys_now();
    sys_timeout(0, pppos_command_wait, (void *)modem);
  }
}

static void pppos_command_run_with_check(
    pppos_modem_t *modem,
    sys_timeout_handler callback,
    const char *cmd,
    const char *result, const char *error,
    pppos_command_result_check check,
    int8_t try_count, uint32_t timeout,
    enum PPPOS_ChatscriptState success_state,
    enum PPPOS_ChatscriptState failure_state)
{
  modem->command.callback = callback;
  modem->command.cmd = cmd;
  modem->command.check = check;
  modem->command.expect_result = result;
  modem->command.expect_error = error;
  modem->command.try_count = try_count;
  modem->command.timeout = timeout;
  modem->failure_state = failure_state;
  modem->success_state = success_state;
  memset(modem->command_ack, 0 , sizeof(modem->command_ack));
  modem->command_ack_len = 0;
  sys_timeout(0, pppos_command_execute, (void *)modem);
}

static void pppos_command_run(
    pppos_modem_t *modem,
    sys_timeout_handler callback,
    const char *cmd,
    const char *result, const char *error,
    int8_t try_count, uint32_t timeout,
    enum PPPOS_ChatscriptState success_state,
    enum PPPOS_ChatscriptState failure_state)
{
  pppos_command_run_with_check(
    modem,
    callback,
    cmd,
    result,
    error,
    NULL,
    try_count,
    timeout,
    success_state,
    failure_state);
}

enum at_result_enum pppos_at_creg_check(const char *ack) {
  int creg_a = -1;
  int creg_b = -1;
  if (sscanf(ack, "\r\n+CREG: %d,%d\r\n",  &creg_a, &creg_b) == 2) {
    if (creg_b == 1 || creg_b == 5) {
      return at_result_success;
    }
  }
  return at_result_failure_error;
}
static void pppos_state_interval(void *arg)
{
  pppos_modem_t *modem = (pppos_modem_t *)arg;
  switch (modem->state) {
  default:
    modem->state = PPPOS_CHATSCRIPT_BEGIN;
    sys_timeout(0, pppos_state_interval, arg);
    break;
  case PPPOS_CHATSCRIPT_BEGIN:
    modem->state = PPPOS_CHATSCRIPT_AT_MODE_PRE_WAIT;
    sys_timeout(1200, pppos_state_interval, arg);
    break;
  case PPPOS_CHATSCRIPT_AT_MODE_PRE_WAIT:
    PPPDEBUG(LOG_DEBUG, ("Trying to exit ppp mode\n"));
    sio_write(modem->sio, (const u8_t *)"+++", 3);
    modem->state = PPPOS_CHATSCRIPT_SEND_PLUS_PLUS_PLUS;
    sys_timeout(0, pppos_state_interval, arg);
    break;
  case PPPOS_CHATSCRIPT_SEND_PLUS_PLUS_PLUS:
    modem->state = PPPOS_CHATSCRIPT_AT_MODE_POST_WAIT;
    sys_timeout(1200, pppos_state_interval, arg);
    break;
  case PPPOS_CHATSCRIPT_AT_MODE_POST_WAIT:
    modem->state = PPPOS_CHATSCRIPT_START_ATE0;
    sys_timeout(0, pppos_state_interval, arg);
    break;
  case PPPOS_CHATSCRIPT_START_ATE0:
#if PPP_DEBUG == LWIP_DBG_ON
        sys_msleep(500);
#endif
        pppos_command_run(
        modem,
        pppos_state_interval,
        "ATE0\r\n",
        "OK\r\n",
        NULL,
        2,
        500,
        PPPOS_CHATSCRIPT_START_CFUN,
        PPPOS_CHATSCRIPT_BEGIN);
    break;
  case PPPOS_CHATSCRIPT_START_CFUN:
    pppos_command_run(
        modem,
        pppos_state_interval,
        "AT+CFUN=1\r\n",
        "OK\r\n",
        NULL,
        1,
        500,
        PPPOS_CHATSCRIPT_START_CREG_QUERY,
        PPPOS_CHATSCRIPT_START_ATE0);
    break;
  case PPPOS_CHATSCRIPT_START_CREG_QUERY:
      pppos_command_run_with_check(
        modem,
        pppos_state_interval,
        "AT+CREG?\r\n",
        "OK\r\n",
        NULL,
        pppos_at_creg_check,
        1,
        500,
        PPPOS_CHATSCRIPT_START_CGDCONT,
        PPPOS_CHATSCRIPT_START_ATE0);
    break;
  case PPPOS_CHATSCRIPT_START_CGDCONT:
    pppos_command_run(
        modem,
        pppos_state_interval,
        /*        "AT+CGDCONT=1,\"IPV4V6\",\"cmnet\"\r\n", */
        "AT+CGDCONT=1,\"IPV4V6\"\r\n",
        "OK\r\n",
        NULL,
        1,
        500,
        PPPOS_CHATSCRIPT_START_CGDCONT_QUERY,
        PPPOS_CHATSCRIPT_START_CGDCONT_QUERY);
    break;
  case PPPOS_CHATSCRIPT_START_CGDCONT_QUERY:
    pppos_command_run(
        modem,
        pppos_state_interval,
        "AT+CGDCONT?\r\n",
        "OK\r\n",
        NULL,
        1,
        500,
        PPPOS_CHATSCRIPT_START_CGDATA,
        PPPOS_CHATSCRIPT_START_ATE0);
    break;
  case PPPOS_CHATSCRIPT_START_CGDATA:
    pppos_command_run(
        modem,
        pppos_state_interval,
        "AT+CGDATA=\"PPP\",1\r\n",
        "CONNECT\r\n",
        NULL,
        1,
        500,
        PPPOS_CHATSCRIPT_ON_CONNECT,
        PPPOS_CHATSCRIPT_START_ATE0);
    break;
  case PPPOS_CHATSCRIPT_ON_CONNECT: {
    u8_t phase_saved = modem->phase_saved;
    u8_t sio_is_reconnected = sio_reconnected(modem->sio);
    if (modem->ppp->phase != phase_saved) {
      modem->phase_saved = modem->ppp->phase;
      if (modem->ppp->phase == PPP_PHASE_DEAD || modem->ppp->phase == PPP_PHASE_TERMINATE) {
        if (phase_saved == 0xFF) {
          modem->command.start_time = sys_now();
          ppp_connect(modem->ppp, 0);
        } else {
          modem->phase_saved = 0xFF;
          modem->state = PPPOS_CHATSCRIPT_BEGIN;
        }
      } else if (modem->ppp->phase == PPP_PHASE_ESTABLISH) {
        modem->command.start_time = sys_now();
      }
    }
    if (modem->ppp->phase != PPP_PHASE_RUNNING) {
      if (modem->ppp->phase == PPP_PHASE_ESTABLISH) {
        if (modem->command.start_time + 2000 < sys_now()) {
          ppp_close(modem->ppp, 0);
        }
      } else {
        if (modem->command.start_time + 15000 < sys_now()) {
          ppp_close(modem->ppp, 0);
        }
      }
    } else if (sio_is_reconnected) {
      ppp_close(modem->ppp, 0);
      modem->phase_saved = 0xFF;
      modem->state = PPPOS_CHATSCRIPT_START_ATE0;
    } else {
      modem->command.start_time = sys_now();
    }

    /* lwip_printfi("On phase %d\n", modem->ppp->phase); */
    sys_timeout(200, pppos_state_interval, arg);
    break;
  }
  }
}

static pppos_modem_t modem;
void pppos_example_init(
  sio_open_option_t *sio_open_option,
  u8_t set_as_default_netif,
  u8_t* sio_buffer,
  u32_t sio_buffer_size)
{
  const char *username = NULL, *password = NULL;
  ip_addr_t dns_ip1;
  ip_addr_t dns_ip2;
  ipaddr_aton("223.5.5.5", &dns_ip1);
  ipaddr_aton("180.76.76.76", &dns_ip2);
  dns_setserver(0, &dns_ip1);
  dns_setserver(1, &dns_ip2);

#if PPPOS_SUPPORT
  memset(&modem, 0, sizeof(modem));
  modem.sio_buffer = sio_buffer;
  modem.sio_buffer_size = sio_buffer_size;
  modem.sio = sio_open(sio_open_option->devnum, sio_open_option->baud_rate);

  if (!modem.sio) {
    lwip_printfe("PPPOS example: Error opening device");
    return;
  }
#ifdef PPP_USERNAME
  username = PPP_USERNAME;
#endif
#ifdef PPP_PASSWORD
  password = PPP_PASSWORD;
#endif

  modem.ppp = pppos_create(&modem.netif, ppp_output_cb, ppp_link_status_cb, (void *)&modem);
  if (!modem.ppp) {
    lwip_printfi("PPPOS example: Could not create PPP control interface");
    return;
  }

  if (set_as_default_netif) {
    netif_set_default(&modem.netif);
  }

  ppp_set_auth(modem.ppp, PPPAUTHTYPE_ANY, username, password);

#if LWIP_NETIF_STATUS_CALLBACK
  netif_set_status_callback(&modem.netif, netif_status_callback);
#endif /* LWIP_NETIF_STATUS_CALLBACK */

  pppos_modem_start(&modem);
#endif /* PPPOS_SUPPORT */
}
