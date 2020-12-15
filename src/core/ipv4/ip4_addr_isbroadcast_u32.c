/**
 * @file
 * This is the IPv4 address tools implementation.
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
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
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/opt.h"

#if LWIP_IPV4

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

/**
 * Determine if an address is a broadcast address on a network interface
 *
 * @param addr address to be checked
 * @param netif the network interface against which the address is checked
 * @return returns non-zero if the address is a broadcast address
 */
u8_t
ip4_addr_isbroadcast_u32(u32_t addr, const struct netif *netif)
{
  ip4_addr_t ipaddr;
  ip4_addr_set_u32(&ipaddr, addr);

  /* all ones (broadcast) or all zeroes (old skool broadcast) */
  if ((~addr == IPADDR_ANY) ||
      (addr == IPADDR_ANY)) {
    return 1;
    /* no broadcast support on this network interface? */
  } else if ((netif->flags & NETIF_FLAG_BROADCAST) == 0) {
    /* the given address cannot be a broadcast address
     * nor can we check against any broadcast addresses */
    return 0;
    /* address matches network interface address exactly? => no broadcast */
  } else if (addr == ip4_addr_get_u32(netif_ip4_addr(netif))) {
    return 0;
    /*  on the same (sub) network... */
  } else if (ip4_addr_net_eq(&ipaddr, netif_ip4_addr(netif), netif_ip4_netmask(netif))
             /* ...and host identifier bits are all ones? =>... */
             && ((addr & ~ip4_addr_get_u32(netif_ip4_netmask(netif))) ==
                 (IPADDR_BROADCAST & ~ip4_addr_get_u32(netif_ip4_netmask(netif))))) {
    /* => network broadcast address */
    return 1;
  } else {
    return 0;
  }
}

#endif /* LWIP_IPV4 */
