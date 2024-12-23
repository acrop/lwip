Import('RTT_ROOT')
Import('rtconfig')
from building import *


cwd     = GetCurrentDir()
#core
src     = Glob('src/core/init.c')
src     += Glob('src/core/def.c')
src     += Glob('src/core/dns.c')
src     += Glob('src/core/inet_chksum.c')
src     += Glob('src/core/ip.c')
src     += Glob('src/core/mem.c')
src     += Glob('src/core/memp.c')
src     += Glob('src/core/netif.c')
src     += Glob('src/core/pbuf.c')
src     += Glob('src/core/raw.c')
src     += Glob('src/core/stats.c')
src     += Glob('src/core/sys.c')
src     += Glob('src/core/altcp.c')
src     += Glob('src/core/altcp_alloc.c')
src     += Glob('src/core/altcp_tcp.c')
src     += Glob('src/core/tcp.c')
src     += Glob('src/core/tcp_in.c')
src     += Glob('src/core/tcp_out.c')
src     += Glob('src/core/timeouts.c')
src     += Glob('src/core/udp.c')

#core4
src     += Glob('src/core/ipv4/acd.c')
src     += Glob('src/core/ipv4/autoip.c')
src     += Glob('src/core/ipv4/dhcp.c')
src     += Glob('src/core/ipv4/etharp.c')
src     += Glob('src/core/ipv4/icmp.c')
src     += Glob('src/core/ipv4/igmp.c')
src     += Glob('src/core/ipv4/ip4_frag.c')
src     += Glob('src/core/ipv4/ip4.c')
src     += Glob('src/core/ipv4/ip4_addr.c')

#api
src     += Glob('src/api/api_lib.c')
src     += Glob('src/api/api_msg.c')
src     += Glob('src/api/err.c')
src     += Glob('src/api/if_api.c')
src     += Glob('src/api/netbuf.c')
src     += Glob('src/api/netdb.c')
src     += Glob('src/api/netifapi.c')
src     += Glob('src/api/sockets.c')
src     += Glob('src/api/tcpip.c')

#ppp
src     += Glob('src/netif/ppp/auth.c')
src     += Glob('src/netif/ppp/ccp.c')
src     += Glob('src/netif/ppp/chap-md5.c')
src     += Glob('src/netif/ppp/chap_ms.c')
src     += Glob('src/netif/ppp/chap-new.c')
src     += Glob('src/netif/ppp/demand.c')
src     += Glob('src/netif/ppp/eap.c')
src     += Glob('src/netif/ppp/ecp.c')
src     += Glob('src/netif/ppp/eui64.c')
src     += Glob('src/netif/ppp/fsm.c')
src     += Glob('src/netif/ppp/ipcp.c')
src     += Glob('src/netif/ppp/ipv6cp.c')
src     += Glob('src/netif/ppp/lcp.c')
src     += Glob('src/netif/ppp/magic.c')
src     += Glob('src/netif/ppp/mppe.c')
src     += Glob('src/netif/ppp/multilink.c')
src     += Glob('src/netif/ppp/ppp.c')
src     += Glob('src/netif/ppp/pppapi.c')
src     += Glob('src/netif/ppp/pppcrypt.c')
src     += Glob('src/netif/ppp/pppoe.c')
src     += Glob('src/netif/ppp/pppol2tp.c')
src     += Glob('src/netif/ppp/pppos.c')
src     += Glob('src/netif/ppp/upap.c')
src     += Glob('src/netif/ppp/utils.c')
src     += Glob('src/netif/ppp/vj.c')
src     += Glob('src/netif/ppp/polarssl/arc4.c')
src     += Glob('src/netif/ppp/polarssl/des.c')
src     += Glob('src/netif/ppp/polarssl/md4.c')
src     += Glob('src/netif/ppp/polarssl/md5.c')
src     += Glob('src/netif/ppp/polarssl/sha1.c')

src     += Glob('contrib/examples/ppp/pppos_example.c')

CPPPATH = [
  cwd,
]

CFLAGS = ' -c -ffunction-sections'

group   = DefineGroup('application', src, depend = [''], CPPPATH = CPPPATH, CFLAGS=CFLAGS)


Return('group')
