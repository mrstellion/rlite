/*
 * RINA TCP/UDP/IPv4 shim IPC process
 *
 *    Vincenzo Maffione <v.maffione@gmail.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/types.h>
#include <rlite/utils.h>
#include "rlite-kernel.h"

#include <linux/module.h>
#include <linux/aio.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/net.h>
#include <linux/file.h>
#include <linux/version.h>
#include <net/sock.h>


struct rina_shim_inet4 {
    struct ipcp_entry *ipcp;
};

struct shim_inet4_flow {
    struct socket *sock;
    struct work_struct rxw;
    void (*sk_data_ready)(struct sock *sk);
};

static void *
rina_shim_inet4_create(struct ipcp_entry *ipcp)
{
    struct rina_shim_inet4 *priv;

    priv = kzalloc(sizeof(*priv), GFP_KERNEL);
    if (!priv) {
        return NULL;
    }

    priv->ipcp = ipcp;

    PD("New IPCP created [%p]\n", priv);

    return priv;
}

static void
rina_shim_inet4_destroy(struct ipcp_entry *ipcp)
{
    struct rina_shim_inet4 *priv = ipcp->priv;

    kfree(priv);

    PD("IPCP [%p] destroyed\n", priv);
}

static void
inet4_rx_worker(struct work_struct *w)
{
    struct shim_inet4_flow *priv =
            container_of(w, struct shim_inet4_flow, rxw);
    struct msghdr msghdr;
    struct socket *sock = priv->sock;
    struct rina_buf *rb;
    struct iovec iov;
    int size;

    PD("called\n");

    memset(&msghdr, 0, sizeof(msghdr));
    msghdr.msg_flags = MSG_DONTWAIT;

    /* Peek length from socket rxq. TODO */
    rb = rina_buf_alloc(1000, 3, GFP_ATOMIC);
    if (!rb) {
        PE("Out of memory\n");
        return;
    }

    iov.iov_base = RINA_BUF_DATA(rb);
    iov.iov_len = rb->len;
    msghdr.msg_iov = &iov;
    msghdr.msg_iovlen = 1;

printk(KERN_CRIT "GOT HERE\n");

    size = sock->ops->recvmsg(NULL, sock, &msghdr, rb->len,
                              MSG_DONTWAIT | MSG_TRUNC);
/*    size = kernel_recvmsg(sk->sk_socket, &msghdr, (struct kvec *)&iov, 1,
                          rb->len, msghdr.msg_flags); */
    if (unlikely(size < 0)) {
        if (size == -EAGAIN) {
            PD("recvmsg(): got EAGAIN\n");
        } else {
            PE("recvmsg(): %d\n", size);
        }
    }

    PD("read %d bytes\n", size);

    rina_buf_free(rb);
}

static void
inet4_data_ready(struct sock *sk)
{
    struct shim_inet4_flow *priv = sk->sk_user_data;

    schedule_work(&priv->rxw);
}

static int
rina_shim_inet4_flow_init(struct ipcp_entry *ipcp, struct flow_entry *flow)
{
    struct shim_inet4_flow *priv;
    struct socket *sock;
    int err;

    priv = kmalloc(sizeof(*priv), GFP_ATOMIC);
    if (!priv) {
        PE("Out of memory\n");
        return -1;
    }

    /* This increments the file descriptor reference counter. */
    sock = sockfd_lookup(flow->cfg.fd, &err);
    if (!sock) {
        PE("Cannot find socket corresponding to file descriptor %d\n", flow->cfg.fd);
        kfree(priv);
        return err;
    }

    write_lock_bh(&sock->sk->sk_callback_lock);
    priv->sk_data_ready = sock->sk->sk_data_ready;
    sock->sk->sk_data_ready = inet4_data_ready;
    sock->sk->sk_user_data = priv;
    write_unlock_bh(&sock->sk->sk_callback_lock);

    PD("Got socket %p\n", sock);

    priv->sock = sock;
    INIT_WORK(&priv->rxw, inet4_rx_worker);
    flow->priv = priv;

    return 0;
}

static int
rina_shim_inet4_flow_deallocated(struct ipcp_entry *ipcp,
                                 struct flow_entry *flow)
{
    struct shim_inet4_flow *priv = flow->priv;
    struct socket *sock;

    if (!priv) {
        return 0;
    }

    sock = priv->sock;

    write_lock_bh(&sock->sk->sk_callback_lock);
    sock->sk->sk_data_ready = priv->sk_data_ready;
    sock->sk->sk_user_data = NULL;
    write_unlock_bh(&sock->sk->sk_callback_lock);

    /* Decrement the file descriptor reference counter, in order to
     * match flow_init(). */
    fput(sock->file);
    flow->priv = NULL;
    kfree(priv);

    PD("Released socket %p\n", sock);

    return 0;
}

static int
rina_shim_inet4_sdu_write(struct ipcp_entry *ipcp,
                      struct flow_entry *flow,
                      struct rina_buf *rb, bool maysleep)
{
    struct shim_inet4_flow *priv= flow->priv;
    struct msghdr msghdr;
    struct iovec iov;
    int ret;

    memset(&msghdr, 0, sizeof(msghdr));
    iov.iov_base = RINA_BUF_DATA(rb);
    iov.iov_len = rb->len;

    msghdr.msg_flags = MSG_DONTWAIT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
    iov_iter_init(&msghdr.msg_iter, WRITE, &iov, 1, rb->len);
    ret = sock_sendmsg(priv->sock, &msghdr, rb->len);
#else
    msghdr.msg_iov = &iov;
    msghdr.msg_iovlen = 1;
    ret = kernel_sendmsg(priv->sock, &msghdr, (struct kvec *)&iov, 1, rb->len);
#endif

    if (unlikely(ret < 0)) {
        PE("sock_sendmsg() failed [%d]\n", ret);
    } else {
        PI("successfully sent %d/%d bytes\n", ret, (int)rb->len);
    }

    rina_buf_free(rb);

    return 0;
}

static int
rina_shim_inet4_config(struct ipcp_entry *ipcp, const char *param_name,
                       const char *param_value)
{
    struct rina_shim_inet4 *priv = (struct rina_shim_inet4 *)ipcp->priv;
    int ret = -EINVAL;

    (void)priv;

    return ret;
}

#define SHIM_DIF_TYPE   "shim-inet4"

static struct ipcp_factory shim_inet4_factory = {
    .owner = THIS_MODULE,
    .dif_type = SHIM_DIF_TYPE,
    .create = rina_shim_inet4_create,
    .ops.destroy = rina_shim_inet4_destroy,
    .ops.flow_allocate_req = NULL, /* Reflect to userspace. */
    .ops.flow_allocate_resp = NULL, /* Reflect to userspace. */
    .ops.flow_init = rina_shim_inet4_flow_init,
    .ops.flow_deallocated = rina_shim_inet4_flow_deallocated,
    .ops.sdu_write = rina_shim_inet4_sdu_write,
    .ops.config = rina_shim_inet4_config,
};

static int __init
rina_shim_inet4_init(void)
{
    return rina_ipcp_factory_register(&shim_inet4_factory);
}

static void __exit
rina_shim_inet4_fini(void)
{
    rina_ipcp_factory_unregister(SHIM_DIF_TYPE);
}

module_init(rina_shim_inet4_init);
module_exit(rina_shim_inet4_fini);
MODULE_LICENSE("GPL");
