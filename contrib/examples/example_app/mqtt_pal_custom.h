

#include <lwip/def.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#define MQTT_PAL_HTONS(s) htons(s)
#define MQTT_PAL_NTOHS(s) ntohs(s)

#define MQTT_PAL_TIME() time(NULL)
typedef time_t mqtt_pal_time_t;

typedef int mqtt_pal_mutex_t;
typedef struct altcp_pcb* mqtt_pal_socket_handle;

#define MQTT_PAL_SOCKET_HANDLE_INVALID ((mqtt_pal_socket_handle)(-1))

#define MQTT_PAL_MUTEX_INIT(mtx_ptr) do {} while (0)
#define MQTT_PAL_MUTEX_LOCK(mtx_ptr) do {} while (0)
#define MQTT_PAL_MUTEX_UNLOCK(mtx_ptr) do {} while (0)
