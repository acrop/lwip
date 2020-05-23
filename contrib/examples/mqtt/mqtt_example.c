
/**
 * @file
 * A simple subscriber program that performs automatic reconnections.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "lwip/mem.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/netif.h"
#include "lwip/log.h"

enum
{
  TCP_DISCONNECTED,
  TCP_CONNECTING,
  TCP_CONNECTED
};

/**
 * @brief A structure that I will use to keep track of some data needed
 *        to setup the connection to the broker.
 *
 * An instance of this struct will be created in my \c main(). Then, whenever
 * \ref reconnect_client is called, this instance will be passed.
 */
struct reconnect_state_t
{
  uint8_t sendbuf[10240];
  uint8_t recvbuf[10240];
  struct mqtt_client client;
  mqtt_pal_socket_handle socketfd;
  ip_addr_t mqtt_ip;
  const char *client_id;
  const char *hostname;
  const char *port;
  const char *topic;
  const char *username;
  const char *password;
  uint16_t keep_alive;
  int mqtt_ip_resolved;
  int conn_state;
  mqtt_pal_socket_handle conn;
  struct pbuf *pal_pbuf;
  int32_t pal_pbuf_offset;
};

/**
 * @brief My reconnect callback. It will reestablish the connection whenever
 *        an error occurs.
 */
void reconnect_client(struct mqtt_client *client, void **reconnect_state_vptr);

/**
 * @brief The function will be called whenever a PUBLISH message is received.
 */
void publish_callback(void **unused, struct mqtt_response_publish *published);

static void mqtt_example_sync(void *arg);
void dns_resolve_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg);
err_t open_lwip_socket(struct reconnect_state_t *client);
void mqtt_example_sending(void *arg);
void mqtt_example_init(void);

void dns_resolve_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
  struct reconnect_state_t *state = callback_arg;
  if (ipaddr)
  {
    state->mqtt_ip_resolved = 1;
    state->mqtt_ip = *ipaddr;
    sys_timeout(1, (sys_timeout_handler)mqtt_example_sync, NULL);
  }
}

/**
 * TCP error callback function. @see tcp_err_fn
 * @param arg MQTT client
 * @param err Error encountered
 */
static void
mqtt_tcp_err_cb(void *arg, err_t err)
{
  struct reconnect_state_t *state = (struct reconnect_state_t *)arg;
  LWIP_UNUSED_ARG(err); /* only used for debug output */
  LWIP_PLATFORM_DIAG(("mqtt_tcp_err_cb: TCP error callback: error %d, arg: %p\n", err, arg));
  LWIP_ASSERT("mqtt_tcp_err_cb: client != NULL", state != NULL);
  /* Set conn to null before calling close as pcb is already deallocated*/
  state->client.socketfd = MQTT_PAL_SOCKET_HANDLE_INVALID;
  state->conn = MQTT_PAL_SOCKET_HANDLE_INVALID;
  state->conn_state = TCP_DISCONNECTED;
}

/**
 * Close connection to server
 * @param client MQTT client
 * @param reason Reason for disconnection
 */
static void
mqtt_close(struct reconnect_state_t *client)
{
  LWIP_ASSERT("mqtt_close: client != NULL", client != NULL);

  /* Bring down TCP connection if not already done */
  if (client->conn != MQTT_PAL_SOCKET_HANDLE_INVALID)
  {
    err_t res;
    altcp_recv(client->conn, NULL);
    altcp_err(client->conn, NULL);
    altcp_sent(client->conn, NULL);
    res = altcp_close(client->conn);
    if (res != ERR_OK)
    {
      altcp_abort(client->conn);
      LWIP_PLATFORM_DIAG(("mqtt_close: Close err=%s\n", lwip_strerr(res)));
    }
    client->client.socketfd = MQTT_PAL_SOCKET_HANDLE_INVALID;
    client->conn = MQTT_PAL_SOCKET_HANDLE_INVALID;
    client->conn_state = TCP_DISCONNECTED;
  }
}

/**
 * TCP received callback function. @see tcp_recv_fn
 * @param arg MQTT client
 * @param p PBUF chain of received data
 * @param err Passed as return value if not ERR_OK
 * @return ERR_OK or err passed into callback
 */
static err_t
mqtt_tcp_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
  struct reconnect_state_t *client = (struct reconnect_state_t *)arg;
  LWIP_ASSERT("mqtt_tcp_recv_cb: client != NULL", client != NULL);
  LWIP_ASSERT("mqtt_tcp_recv_cb: client->conn == pcb", client->conn == pcb);
  if ((err != ERR_OK) || (p == NULL)) {
    /* error or closed by other side? */
    if (p != NULL) {
      /* Inform TCP that we have taken the data. */
      altcp_recved(pcb, p->tot_len);
      pbuf_free(p);
    }
    LWIP_PLATFORM_DIAG(("mqtt_tcp_recv_cb: Recv pbuf=NULL, remote has closed connection\n"));
    mqtt_close(client);
    return ERR_OK;
  }


  /* Tell remote that data has been received */
  altcp_recved(pcb, p->tot_len);
  client->client.number_of_timeouts = 0;
  LWIP_PLATFORM_DIAG(("mqtt_tcp_recv_cb: received tcp with len %d\n", p->tot_len));
  client->pal_pbuf = p;
  client->pal_pbuf_offset = 0;
  mqtt_sync(&client->client);
  if (client->pal_pbuf_offset < client->pal_pbuf->tot_len)
  {
    LWIP_PLATFORM_DIAG(("mqtt_tcp_recv_cb: mqtt_sync didn't receive all pal pbuf\n"));
    mqtt_close(client);
  }
  client->pal_pbuf = NULL;
  client->pal_pbuf_offset = 0;
  pbuf_free(p);
  return ERR_OK;
}

ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void *buf, size_t len, int flags)
{
  ssize_t sent;
  if (fd == MQTT_PAL_SOCKET_HANDLE_INVALID)
  {
    return 0;
  }
  {
    u16_t send_len = altcp_sndbuf(fd);
    sent = send_len < len ? send_len : (ssize_t)len;
    if (sent <= 0)
    {
      return 0;
    }
  }
  {
    err_t err = altcp_write(fd, buf, (u16_t)sent, TCP_WRITE_FLAG_COPY);
    return err != ERR_OK ? MQTT_ERROR_SOCKET_ERROR : sent;
  }
}

ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void *buf, size_t bufsz, int flags)
{
  struct reconnect_state_t *state;
  if (fd == MQTT_PAL_SOCKET_HANDLE_INVALID)
  {
    return 0;
  }
  state = (struct reconnect_state_t *)fd->arg;
  if (state->pal_pbuf == NULL)
  {
    return 0;
  }

  {
    int32_t pal_remain_len = state->pal_pbuf->tot_len - state->pal_pbuf_offset;
    int32_t len_to_copy = pal_remain_len < (int32_t)bufsz ? pal_remain_len : (int32_t)bufsz;
    if (len_to_copy > 0)
    {
      int32_t len_copied = pbuf_copy_partial(state->pal_pbuf, buf, len_to_copy, state->pal_pbuf_offset);
      state->pal_pbuf_offset += len_copied;
      return len_copied;
    }
  }
  return 0;
}

/**
 * TCP connect callback function. @see tcp_connected_fn
 * @param arg MQTT client
 * @param err Always ERR_OK, mqtt_tcp_err_cb is called in case of error
 * @return ERR_OK
 */
static err_t
mqtt_tcp_connect_cb(void *arg, struct altcp_pcb *tpcb, err_t err)
{
  struct reconnect_state_t *state = (struct reconnect_state_t *)arg;

  if (err != ERR_OK)
  {
    LWIP_PLATFORM_DIAG(("mqtt_tcp_connect_cb: TCP connect error %d\n", err));
    return err;
  }

  /* Initiate receiver state */

  /* Setup TCP callbacks */
  altcp_recv(tpcb, mqtt_tcp_recv_cb);
#if 0
  altcp_sent(tpcb, mqtt_tcp_sent_cb);
  altcp_poll(tpcb, mqtt_tcp_poll_cb, 2);
#endif

  LWIP_PLATFORM_DIAG(("mqtt_tcp_connect_cb: TCP connection established to server\n"));
  /* Enter MQTT connect state */
  state->conn_state = TCP_CONNECTED;

  return ERR_OK;
}

char mqtt_own_ip_name[32];
ip_addr_t mqtt_dns_resolve;
/*
    A template for opening a non-blocking POSIX socket.
*/
err_t open_lwip_socket(struct reconnect_state_t *client)
{
  err_t err;
  mqtt_pal_socket_handle sockfd;
  const ip_addr_t *ip_addr = &client->mqtt_ip;
  uint16_t port = (uint16_t)atoi(client->port);
  if (!client->mqtt_ip_resolved)
  {
    if (netif_default == NULL)
    {
      return ERR_INPROGRESS;
    }
    ip4addr_ntoa_r(netif_ip4_addr(netif_default), mqtt_own_ip_name, sizeof(mqtt_own_ip_name));
    if (strcmp(mqtt_own_ip_name, "0.0.0.0") == 0)
    {
      return ERR_INPROGRESS;
    }
    dns_gethostbyname(client->hostname, &mqtt_dns_resolve, dns_resolve_callback, client);
    return ERR_INPROGRESS;
  }

  sockfd = altcp_tcp_new_ip_type(IP_GET_TYPE(&(client->mqtt_ip)));
  if (sockfd == NULL)
  {
    return ERR_MEM;
  }
  client->conn = sockfd;

  /* Set arg pointer for callbacks */
  altcp_arg(client->conn, client);
  /* Any local address, pick random local port number */
  err = altcp_bind(client->conn, IP_ADDR_ANY, 0);
  if (err != ERR_OK)
  {
    LWIP_PLATFORM_DIAG(("mqtt_client_connect: Error binding to local ip/port, %d\n", err));
    goto tcp_fail;
  }
  LWIP_PLATFORM_DIAG(("mqtt_client_connect: Connecting to host: %s at port:%" U16_F "\n", ipaddr_ntoa(ip_addr), port));

  /* Connect to server */
  err = altcp_connect(client->conn, ip_addr, port, mqtt_tcp_connect_cb);
  if (err != ERR_OK)
  {
    LWIP_PLATFORM_DIAG(("mqtt_client_connect: Error connecting to remote ip/port, %d\n", err));
    goto tcp_fail;
  }
  /* Set error callback */
  altcp_err(client->conn, mqtt_tcp_err_cb);
  client->conn_state = TCP_CONNECTING;
  return ERR_OK;
tcp_fail:
  client->conn = MQTT_PAL_SOCKET_HANDLE_INVALID;
  altcp_abort(sockfd);
  return err;
}

void reconnect_client(struct mqtt_client *client, void **reconnect_state_vptr)
{
  uint8_t connect_flags;
  struct reconnect_state_t *reconnect_state = *((struct reconnect_state_t **)reconnect_state_vptr);

  /* Close the clients socket if this isn't the initial reconnect call */
  if (client->error != MQTT_ERROR_INITIAL_RECONNECT)
  {
    mqtt_close(reconnect_state);
    lwip_printfi("reconnect_client: called while client was in error state \"%s\"\n",
           mqtt_error_str(client->error));
    MQTT_PAL_MUTEX_UNLOCK(&client->mutex);
    return;
  }
  if (reconnect_state->conn_state != TCP_CONNECTED)
  {
    MQTT_PAL_MUTEX_UNLOCK(&client->mutex);
    return;
  }
  /* Reinitialize the client. */
  mqtt_reinit(client, reconnect_state->conn,
              reconnect_state->sendbuf, sizeof(reconnect_state->sendbuf),
              reconnect_state->recvbuf, sizeof(reconnect_state->recvbuf));
  client->response_timeout = 60;

  /* Ensure we have a clean session */
  connect_flags = MQTT_CONNECT_CLEAN_SESSION;
  /* Send connection request to the broker. */
  mqtt_connect(client, reconnect_state->client_id, NULL, NULL, 0,
               reconnect_state->username, reconnect_state->password, connect_flags, reconnect_state->keep_alive);

  /* Subscribe to the topic. */
  mqtt_subscribe(client, reconnect_state->topic, 0);
}

void publish_callback(void **unused, struct mqtt_response_publish *published)
{
  /* note that published->topic_name is NOT null-terminated (here we'll change it to a c-string) */
  char *topic_name = (char *)malloc(published->topic_name_size + 1);
  memcpy(topic_name, published->topic_name, published->topic_name_size);
  topic_name[published->topic_name_size] = '\0';

  if (published->application_message_size > 100)
  {
    lwip_printfi("Received publish('%s'): %d\n", topic_name, (int)published->application_message_size);
  }
  else
  {
    lwip_printfi("Received publish('%s'): %s\n", topic_name, (const char *)published->application_message);
  }

  free(topic_name);
}

/* setup a client */
struct reconnect_state_t reconnect_state;

static void mqtt_example_sync(void *arg)
{
  int open_failed = 0;
  if (reconnect_state.conn_state == TCP_CONNECTED)
  {
    mqtt_sync(&reconnect_state.client);
    if (reconnect_state.client.number_of_timeouts >= 3)
    {
      reconnect_state.client.number_of_timeouts = 0;
      mqtt_close(&reconnect_state);
    }
  }
  else if (reconnect_state.conn_state == TCP_DISCONNECTED)
  {
    reconnect_state.pal_pbuf = NULL;
    reconnect_state.pal_pbuf_offset = 0;
    if (open_lwip_socket(&reconnect_state) == ERR_OK)
    {
      reconnect_state.client.error = MQTT_ERROR_INITIAL_RECONNECT;
    } else {
      open_failed = 1;
      reconnect_state.client.error = MQTT_ERROR_SOCKET_ERROR;
    }
  }
  else
  {
    /* Waiting tcp connected */
  }

  if (!open_failed && reconnect_state.mqtt_ip_resolved)
  {
    sys_timeout(1, (sys_timeout_handler)mqtt_example_sync, NULL);
  }
  else
  {
    sys_timeout(1000, (sys_timeout_handler)mqtt_example_sync, NULL);
  }
}

static char message_to_send[8192];

void mqtt_example_sending(void *arg)
{
  enum MQTTErrors err;
  sprintf(message_to_send, "%lld test_publish_topicjdkgajewgaje;wlg", time(NULL));
  err = mqtt_publish(&reconnect_state.client, "lwip_mqtt_c_test_topic_publish", message_to_send, sizeof(message_to_send), MQTT_PUBLISH_QOS_0);
  if (MQTT_OK != err)
  {
    LWIP_PLATFORM_DIAG(("mqtt_publish with error \"%s\"\n", mqtt_error_str(err)));
  }
  sys_timeout(5000, (sys_timeout_handler)mqtt_example_sending, NULL);
}

void mqtt_example_init(void)
{
  /* build the reconnect_state structure which will be passed to reconnect */
  memset((void *)&reconnect_state, 0, sizeof(reconnect_state));
  reconnect_state.client_id = "LWIP-MQTT-C-Client-Test";
  reconnect_state.hostname = "emqx-test.growlogin.net";
  reconnect_state.port = "1883";
  reconnect_state.topic = "lwip_mqtt_c_test_topic_subscribe";
  reconnect_state.username = "growlogin";
  reconnect_state.password = "pass";
  reconnect_state.keep_alive = 100;

  mqtt_init_reconnect(&reconnect_state.client,
                      reconnect_client, &reconnect_state,
                      publish_callback);
  reconnect_state.mqtt_ip_resolved = 0;
  sys_timeout(1, (sys_timeout_handler)mqtt_example_sync, NULL);
  sys_timeout(3000, (sys_timeout_handler)mqtt_example_sending, NULL);
}
