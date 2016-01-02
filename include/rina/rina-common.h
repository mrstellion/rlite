#ifndef __RINA_COMMON_H__
#define __RINA_COMMON_H__


#define RINA_IPCM_UNIX_NAME     "/var/rina/ipcm"

/* Application naming information:
 *   - Application Process Name
 *   - Application Process Instance
 *   - Application Entity Name
 *   - Application Entity Instance
 */
struct rina_name {
    char *apn;
    char *api;
    char *aen;
    char *aei;
} __attribute__((packed));

typedef uint16_t rina_msg_t;

/* All the possible messages begin like this. */
struct rina_msg_base {
    rina_msg_t msg_type;
    uint32_t event_id;
} __attribute__((packed));

/* A simple response message layout that can be shared by many
 * different types. */
struct rina_msg_base_resp {
    rina_msg_t msg_type;
    uint32_t event_id;

    uint8_t result;
} __attribute__((packed));

/* Bind the flow identified by port_id to
 * this rina_io device. */
#define RINA_IO_MODE_APPL_BIND    86
/* The same as APPL_BIND, but also enable the IPCP identified
 * by ipcp_id to receive SDUs (in kernel) from lower layers. */
#define RINA_IO_MODE_IPCP_BIND    87
/* Use this device to write/read management
 * PDUs for the IPCP specified by ipcp_id. */
#define RINA_IO_MODE_IPCP_MGMT    88

struct rina_ioctl_info {
    uint8_t mode;
    uint32_t port_id;
    uint16_t ipcp_id;
} __attribute__((packed));

#define RINA_MGMT_HDR_T_OUT_LOCAL_PORT      1
#define RINA_MGMT_HDR_T_OUT_DST_ADDR        2
#define RINA_MGMT_HDR_T_IN                  3

/* Header used across user/kernel boundary when writing/reading
 * management SDUs from rina-io devices working in RINA_IO_MODE_IPCP_MGMT
 * mode.
 * Userspace can write a management SDU specifying either a local
 * port (type OUT_LOCAL_PORT) or a destination address (OUT_DST_ADDR). In
 * the former case 'local_port' should refer to an existing N-1 flow
 * ('remote_addr' is ignored), while in the latter 'remote_addr' should
 * refer to an N-IPCP that will be reached as specified by the PDUFT
 * ('local_port' is ignored).
 * When reading a management SDU, the header will contain the local port
 * where the SDU was received and the source (remote) address that sent it.
 */
struct rina_mgmt_hdr {
    uint8_t type;
    uint32_t local_port;
    uint64_t remote_addr;
} __attribute__((packed));

/* Logging macros. */
#define PD_ON  /* Enable debug print. */
#define PI_ON  /* Enable info print. */

#ifdef __KERNEL__
#define PRINTFUN printk
#else
#define PRINTFUN printf
#endif

#ifdef PD_ON
#define PD(format, ...) PRINTFUN(format, ##__VA_ARGS__)
#else
#define PD(format, ...)
#endif

#ifdef PI_ON
#define PI(format, ...) PRINTFUN(format, ##__VA_ARGS__)
#else
#define PI(formt, ...)
#endif

#define PN(format, ...)
#define PE(format, ...) PRINTFUN(format, ##__VA_ARGS__)

#endif  /* __RINA_COMMON_H__ */
