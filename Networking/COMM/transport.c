#include <string.h>   /* memcpy, memmove */
#include <stdlib.h>
#include <stdio.h>    /* vsnprintf */
#include <stdarg.h>   /* va_list, va_start, va_end */
#include "transport.h"
#include "frame_codec.h"
#include "dispatcher.h"
#include "UART_Logging.h"
#include "IfxStm.h"    /* STM for latency measurement */
#include "Configuration.h"  /* IFX_CFG_STM_TICKS_PER_MS */

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

/* Debug print via the SAME UART that lwIP uses (UART_Logging).
   Ifx_Console_print does NOT work — Ifx_Console_init() is never called. */
static void dbg_print(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        sendUARTMessage(buf, (Ifx_SizeT)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
    }
}

/* UDP telemetry target */
#define TELEMETRY_DEST_PORT 6000

/* TCP command server */
#define TCP_CMD_PORT        6001

/* TCP RX buffer */
#define TCP_RX_BUF_SIZE     256

static struct udp_pcb *s_udp_pcb = NULL;
static struct tcp_pcb *s_tcp_listen_pcb = NULL;
static struct tcp_pcb *s_tcp_client_pcb = NULL;

static uint8_t s_tcp_rx_buf[TCP_RX_BUF_SIZE];
static uint16_t s_tcp_rx_len = 0;

/* Deferred reply mechanism — send outside callback context */
static uint8_t s_pending_reply_buf[256];
static uint8_t s_pending_reply_len = 0;
static uint8_t s_pending_reply_flag = 0;

/* Forward declarations */
static err_t transport_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t transport_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  transport_tcp_err(void *arg, err_t err);
static err_t transport_tcp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t transport_tcp_poll(void *arg, struct tcp_pcb *tpcb);
static void  transport_tcp_close(struct tcp_pcb *tpcb);

/* ================================================================== */
/* Helper: attempt to flush pending reply via tcp_write + tcp_output   */
/* Only clears the flag when tcp_write returns ERR_OK (success).       */
/* If tcp_write fails (ERR_MEM, ERR_VAL, etc.) the flag stays set     */
/* so the next poll / tcp_poll callback can retry.                    */
/* ================================================================== */
static void transport_try_send_pending(void)
{
    if (!s_pending_reply_flag || s_tcp_client_pcb == NULL)
        return;

    u16_t sndbuf = tcp_sndbuf(s_tcp_client_pcb);
    u8_t  state  = s_tcp_client_pcb->state;

    dbg_print(">>> SEND: flag=%d sndbuf=%d state=%d len=%d\r\n",
              s_pending_reply_flag, sndbuf, state, s_pending_reply_len);

    err_t werr = tcp_write(s_tcp_client_pcb,
                           s_pending_reply_buf,
                           s_pending_reply_len,
                           TCP_WRITE_FLAG_COPY);
    dbg_print(">>> SEND: tcp_write=%d\r\n", werr);

    if (werr == ERR_OK)
    {
        err_t oerr = tcp_output(s_tcp_client_pcb);
        dbg_print(">>> SEND: tcp_output=%d  OK reply flushed\r\n", oerr);
        s_pending_reply_flag = 0;   /* clear only on success */
    }
    else
    {
        dbg_print(">>> SEND: tcp_write FAILED, will retry\r\n");
        /* flag stays set — transport_poll / tcp_poll will retry */
    }
}

/* ================================================================== */
/* Helper: properly close a TCP connection                             */
/* Removes all callbacks BEFORE tcp_close to prevent stale callbacks   */
/* on a closing PCB. Falls back to tcp_abort if tcp_close fails.       */
/* ================================================================== */
static void transport_tcp_close(struct tcp_pcb *tpcb)
{
    if (tpcb == NULL) return;

    tcp_arg(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    err_t err = tcp_close(tpcb);
    if (err != ERR_OK) {
        dbg_print(">>> CLOSE: tcp_close=%d, aborting\r\n", err);
        tcp_abort(tpcb);
    } else {
        dbg_print(">>> CLOSE: tcp_close OK\r\n");
    }
}

/* ================================================================== */
/* UDP / Telemetry TX                                                 */
/* ================================================================== */

void transport_init(void)
{
    /* UDP PCB for sending telemetry */
    s_udp_pcb = udp_new();
    if (s_udp_pcb != NULL) {
        udp_bind(s_udp_pcb, IP_ADDR_ANY, 0);
    }

    /* TCP server for commands/events */
    s_tcp_listen_pcb = tcp_new();
    if (s_tcp_listen_pcb != NULL) {
        if (tcp_bind(s_tcp_listen_pcb, IP_ADDR_ANY, TCP_CMD_PORT) == ERR_OK) {
            struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(s_tcp_listen_pcb, 1);
            if (listen_pcb != NULL) {
                s_tcp_listen_pcb = listen_pcb;
                tcp_accept(s_tcp_listen_pcb, transport_tcp_accept);
            }
        }
    }

    s_tcp_rx_len = 0;
    s_pending_reply_flag = 0;
}

uint8_t transport_send_telemetry(const uint8_t *buf, uint8_t len)
{
    if (s_udp_pcb == NULL || buf == NULL || len == 0) {
        return 0;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL) {
        return 0;
    }

    memcpy(p->payload, buf, len);

    ip_addr_t dest_ip;
    IP4_ADDR(&dest_ip, 192, 168, 10, 10);

    err_t err = udp_sendto(s_udp_pcb, p, &dest_ip, TELEMETRY_DEST_PORT);
    pbuf_free(p);

    return (err == ERR_OK) ? 1 : 0;
}

/* ================================================================== */
/* TCP / Command RX + Event TX                                        */
/* ================================================================== */

static err_t transport_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    /* One client at a time */
    if (s_tcp_client_pcb != NULL) {
        tcp_close(newpcb);
        return ERR_OK;
    }

    s_tcp_client_pcb = newpcb;
    s_tcp_rx_len = 0;
    s_pending_reply_flag = 0;

    tcp_arg(newpcb, newpcb);                          /* pass PCB as arg for tcp_err identification */
    tcp_recv(newpcb, transport_tcp_recv);
    tcp_sent(newpcb, transport_tcp_sent);
    tcp_err(newpcb, transport_tcp_err);
    tcp_poll(newpcb, transport_tcp_poll, 1);          /* poll every ~500ms as safety net */
    tcp_nagle_disable(newpcb);                        /* send small replies immediately  */

    dbg_print(">>> ACCEPT: client connected, state=%d\r\n", newpcb->state);
    return ERR_OK;
}

static err_t transport_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        return err;
    }

    if (p == NULL) {
        /* Remote closed connection */
        dbg_print(">>> RECV: client closed (p=NULL)\r\n");
        if (tpcb == s_tcp_client_pcb) {
            s_tcp_client_pcb = NULL;
            s_tcp_rx_len = 0;
            s_pending_reply_flag = 0;
        }
        transport_tcp_close(tpcb);
        return ERR_OK;
    }

    uint16_t copy_len = p->tot_len;
    if ((s_tcp_rx_len + copy_len) > TCP_RX_BUF_SIZE) {
        /* Buffer overflow — drop connection to resync */
        pbuf_free(p);
        if (tpcb == s_tcp_client_pcb) {
            s_tcp_client_pcb = NULL;
            s_tcp_rx_len = 0;
            s_pending_reply_flag = 0;
        }
        transport_tcp_close(tpcb);
        return ERR_OK;
    }

    /* Copy pbuf chain into linear buffer */
    pbuf_copy_partial(p, &s_tcp_rx_buf[s_tcp_rx_len], copy_len, 0);
    s_tcp_rx_len += copy_len;
    pbuf_free(p);

    /* Timestamp when data enters our layer (for latency measurement) */
    uint32_t t_rx = (uint32_t)IfxStm_get(&MODULE_STM0);

    /* Parse complete frames */
    uint8_t cmd_type, seq, payload_len;
    const uint8_t *payload;
    uint16_t consumed = 0;

    while (consumed < s_tcp_rx_len) {
        uint8_t ret = frame_codec_unpack(&s_tcp_rx_buf[consumed],
                                          s_tcp_rx_len - consumed,
                                          &cmd_type, &seq,
                                          &payload, &payload_len);
        if (ret == FRAME_UNPACK_INCOMPLETE) {
            break; /* not enough data yet */
        }

        if (ret == FRAME_UNPACK_CRC_ERROR) {
            uint8_t frame_size = FRAME_HEADER_SIZE + payload_len + FRAME_CRC_SIZE;
            dbg_print(">>> CRC ERROR: cmd=0x%02X seq=%d len=%d — dropping frame\r\n",
                      cmd_type, seq, payload_len);
            consumed += frame_size;
            continue; /* skip corrupted frame */
        }

        /* ret == FRAME_UNPACK_OK — measure dispatch latency */
        uint32_t t_dispatch = (uint32_t)IfxStm_get(&MODULE_STM0);
        uint32_t delta_ticks = t_dispatch - t_rx;
        uint32_t delta_us = delta_ticks / (IFX_CFG_STM_TICKS_PER_MS / 1000);

        dispatcher_dispatch(cmd_type, seq, payload, payload_len);

        dbg_print(">>> DISPATCH: cmd=0x%02X seq=%d latency=%d us\r\n",
                  cmd_type, seq, delta_us);

        uint8_t frame_size = FRAME_HEADER_SIZE + payload_len + FRAME_CRC_SIZE;
        consumed += frame_size;
    }

    /* Shift remaining bytes to front */
    if (consumed > 0 && consumed < s_tcp_rx_len) {
        memmove(&s_tcp_rx_buf[0], &s_tcp_rx_buf[consumed], s_tcp_rx_len - consumed);
        s_tcp_rx_len -= consumed;
    } else if (consumed == s_tcp_rx_len) {
        s_tcp_rx_len = 0;
    }

    tcp_recved(tpcb, copy_len);

    /* Try immediate send of any pending reply (handler may have queued one) */
    transport_try_send_pending();

    return ERR_OK;
}

static err_t transport_tcp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)arg;
    (void)tpcb;
    (void)len;
    return ERR_OK;
}

static void transport_tcp_err(void *arg, err_t err)
{
    struct tcp_pcb *closed_pcb = (struct tcp_pcb *)arg;
    dbg_print(">>> TCP_ERR: err=%d\r\n", err);
    /* Only clear if this is the CURRENT client — not a stale PCB
       from a previous connection that was already replaced */
    if (closed_pcb != NULL && closed_pcb == s_tcp_client_pcb) {
        dbg_print(">>> TCP_ERR: clearing current client\r\n");
        s_tcp_client_pcb = NULL;
        s_tcp_rx_len = 0;
        s_pending_reply_flag = 0;
    } else {
        dbg_print(">>> TCP_ERR: stale PCB, ignoring\r\n");
    }
}

/* lwIP periodic poll callback — safety net to retry failed sends */
static err_t transport_tcp_poll(void *arg, struct tcp_pcb *tpcb)
{
    (void)arg;
    if (s_pending_reply_flag && tpcb == s_tcp_client_pcb)
    {
        dbg_print(">>> TCP_POLL: retrying pending reply\r\n");
        transport_try_send_pending();
    }
    return ERR_OK;
}

uint8_t transport_send_event(const uint8_t *buf, uint8_t len)
{
    if (s_tcp_client_pcb == NULL || buf == NULL || len == 0) {
        dbg_print(">>> EVENT: FAIL no client/buf (pcb=%d len=%d)\r\n",
                  (s_tcp_client_pcb != NULL) ? 1 : 0, len);
        return 0;
    }

    /* Queue data for deferred send (outside callback context) */
    if (len > sizeof(s_pending_reply_buf)) {
        return 0;
    }
    memcpy(s_pending_reply_buf, buf, len);
    s_pending_reply_len = len;
    s_pending_reply_flag = 1;

    dbg_print(">>> EVENT: queued %d bytes, trying immediate send\r\n", len);

    /* Try immediate send — works if called from main loop.
       If called from inside recv callback, tcp_write should still
       work (Echo.c does this); tcp_output may be deferred by lwIP
       to after tcp_input returns. */
    transport_try_send_pending();

    return 1;
}

void transport_poll(void)
{
    /* Retry any pending reply that failed in the callback */
    if (s_pending_reply_flag && s_tcp_client_pcb != NULL) {
        transport_try_send_pending();
    }
}
