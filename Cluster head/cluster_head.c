/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ipv6/uip.h"
#include "net/routing/rpl-classic/rpl.h"

#include "net/netstack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "cc2420.h"

#define DEBUG DEBUG_PRINT
#include "net/ipv6/uip-debug.h"

#ifndef UIP_IP_BUF
#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#endif

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 6666
#define UDP_SERVER1_PORT 8888
#define UDP_BORDER_PORT 7777
#define MCAST_SINK_UDP_PORT 3001    /* Host byte order */
#define MCAST_SINK_UDP_PORT_CH 3002 /* Host byte order */

#define SERVER_REPLY 1
#define MAX_PAYLOAD_LEN 3

#define UDP_EXAMPLE_ID 190
#define CH_MULTICAST_INTERVAL 180
#define CH_ELECTION_INTERVAL 300

static struct uip_udp_conn *server_conn;
static struct uip_udp_conn *border_conn;

static uip_ipaddr_t border_ipaddr;

static struct uip_udp_conn *mcast_conn;
static struct uip_udp_conn *mcast_conn_ch;
static struct uip_udp_conn *ch_conn;
static char buf[MAX_PAYLOAD_LEN];

static int *randomNumberCH;
static int ch_can_send = 1;

PROCESS(udp_server_process, "UDP server process");
AUTOSTART_PROCESSES(&udp_server_process);

/*---------------------------------------------------------------------------*/
static void
prepare_mcast(void)
{
  uip_ipaddr_t ipaddr;

#if UIP_MCAST6_CONF_ENGINE == UIP_MCAST6_ENGINE_MPL
  /*
   * MPL defines a well-known MPL domain, MPL_ALL_FORWARDERS, which
   *  MPL nodes are automatically members of. Send to that domain.
   */
  uip_ip6addr(&ipaddr, 0xFF03, 0, 0, 0, 0, 0, 0, 0xFC);
#else
  /*
   * IPHC will use stateless multicast compression for this destination
   * (M=1, DAC=0), with 32 inline bits (1E 89 AB CD)
   */
  uip_ip6addr(&ipaddr, 0xFF1E, 0, 0, 0, 0, 0, 0x89, 0xABCD);
#endif
  mcast_conn = udp_new(&ipaddr, UIP_HTONS(MCAST_SINK_UDP_PORT), NULL);
}

/*---------------------------------------------------------------------------*/
static void
multicast_send(void)
{
  sprintf(buf, "CH");
  PRINTF("Sending multicast data '%s' to ", buf);
  PRINT6ADDR(&mcast_conn->ripaddr);
  PRINTF("\n");

  uip_udp_packet_send(mcast_conn, buf, strlen(buf));
}

/*---------------------------------------------------------------------------*/
static void
prepare_mcast_ch(void)
{
  uip_ipaddr_t ipaddr;

#if UIP_MCAST6_CONF_ENGINE == UIP_MCAST6_ENGINE_MPL
  /*
   * MPL defines a well-known MPL domain, MPL_ALL_FORWARDERS, which
   *  MPL nodes are automatically members of. Send to that domain.
   */
  uip_ip6addr(&ipaddr, 0xFF03, 0, 0, 0, 0, 0, 0, 0xFC);
#else
  /*
   * IPHC will use stateless multicast compression for this destination
   * (M=1, DAC=0), with 32 inline bits (1E 89 AB CD)
   */
  uip_ip6addr(&ipaddr, 0xFF1E, 0, 0, 0, 0, 0, 0x89, 0xABCD);
#endif
  mcast_conn_ch = udp_new(&ipaddr, UIP_HTONS(MCAST_SINK_UDP_PORT_CH), NULL);
}

/*---------------------------------------------------------------------------*/
static void
multicast_send_ch(void)
{
  *randomNumberCH = rand() % 100 + 1;
  PRINTF("Sending multicast data '%d' to ", *randomNumberCH);
  PRINT6ADDR(&mcast_conn_ch->ripaddr);
  PRINTF("\n");

  uip_udp_packet_send(mcast_conn_ch, randomNumberCH, 3);
}

/*---------------------------------------------------------------------------*/
static void
send_packet(void *ptr, char *buf, uip_ipaddr_t dest_ipaddr, struct uip_udp_conn *dest_conn, int rem_port)
{
  PRINTF("Sending data '%s'  ", buf);
  PRINT6ADDR(&dest_ipaddr);
  PRINTF("\n");
  uip_udp_packet_sendto(dest_conn, buf, strlen(buf), &dest_ipaddr, UIP_HTONS(rem_port));
}

/*---------------------------------------------------------------------------*/
static uip_ds6_maddr_t *
join_mcast_group_ch(void)
{
  uip_ipaddr_t addr;
  uip_ds6_maddr_t *rv;
  const uip_ipaddr_t *default_prefix = uip_ds6_default_prefix();

  /* First, set our v6 global */
  uip_ip6addr_copy(&addr, default_prefix);
  uip_ds6_set_addr_iid(&addr, &uip_lladdr);
  uip_ds6_addr_add(&addr, 0, ADDR_AUTOCONF);

  /*
   * IPHC will use stateless multicast compression for this destination
   * (M=1, DAC=0), with 32 inline bits (1E 89 AB CD)
   */
  uip_ip6addr(&addr, 0xFF1E, 0, 0, 0, 0, 0, 0x89, 0xABCD);
  rv = uip_ds6_maddr_add(&addr);

  if (rv)
  {
    PRINTF("Joined multicast group ");
    PRINT6ADDR(&uip_ds6_maddr_lookup(&addr)->ipaddr);
    PRINTF("\n");
  }
  return rv;
}

/*---------------------------------------------------------------------------*/
static signed char
calculate_RSSI(uip_ipaddr_t originator_ipaddr)
{
  static signed char rss;
  static signed char rss_val;
  static signed char rss_offset;
  rss_val = cc2420_last_rssi;
  rss_offset = 0;
  rss = rss_val + rss_offset;
  PRINTF("RSSI of Last Packet Received is %d dBm from ", rss);
  PRINT6ADDR(&originator_ipaddr);

  // Calculate LQI
  radio_value_t *lqi = malloc(sizeof(radio_value_t));
  cc2420_driver.get_value(RADIO_PARAM_LAST_LINK_QUALITY, lqi);

  PRINTF("\nLQI = %d\n", *lqi);

  free(lqi);

  return rss;
}

/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  char *appdata;

  if (uip_newdata())
  {
    if (isdigit(uip_newdata()))
    {
      if (uip_newdata() > *randomNumberCH && ch_can_send == 1)
      {
        ch_can_send = 0;
      }
      else
      {
        ch_can_send = 1;
      }
    }
    else
    {
      appdata = (char *)uip_appdata;
      appdata[uip_datalen()] = 0;
      uip_ipaddr_t client_ipaddr = UIP_IP_BUF->srcipaddr;
      PRINTF("DATA recv '%s' from ", appdata);
      PRINT6ADDR(&client_ipaddr);
      PRINTF("\n");

      signed char rss = calculate_RSSI(client_ipaddr);
      // Try to redirect data to the border router
      send_packet(NULL, appdata, border_ipaddr, border_conn, UDP_BORDER_PORT);
      /* --------------------------------------------------- */
      // Send RSSI to client
      char *rssi = malloc(4 * sizeof(char));
      snprintf(rssi, 4, "%d", rss);
      send_packet(NULL, rssi, client_ipaddr, server_conn, UDP_CLIENT_PORT);
      free(rssi);
    }
  }
  else
  {
    PRINTF("No DATA\n");
  }
}

/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Server IPv6 addresses: ");
  for (i = 0; i < UIP_DS6_ADDR_NB; i++)
  {
    state = uip_ds6_if.addr_list[i].state;
    if (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)
    {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE)
      {
        uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
}

/*---------------------------------------------------------------------------*/
static void
set_global_address(void)
{
  uip_ip6addr(&border_ipaddr, 0xfd00, 0, 0, 0x5000, 0, 0, 0, 1); //  fd00:0:0:5000::1
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{
  static struct etimer et;
  static struct etimer et_ch;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  PRINTF("Cluster head started. nbr:%d routes:%d\n",
         NBR_TABLE_CONF_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES);

  set_global_address();

  print_local_addresses();

  // Connection to the client
  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);
  // Connection to the border router
  border_conn = udp_new(NULL, UIP_HTONS(UDP_BORDER_PORT), NULL);
  if (server_conn == NULL)
  {
    PRINTF("No UDP connection available with the client, exiting the process!\n");
    PROCESS_EXIT();
  }

  if (border_conn == NULL)
  {
    PRINTF("No UDP connection available with the border router, exiting the process!\n");
    PROCESS_EXIT();
  }

  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));
  udp_bind(border_conn, UIP_HTONS(UDP_SERVER1_PORT));

  PRINTF("Created a server connection with remote address client ");
  PRINT6ADDR(&server_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
         UIP_HTONS(server_conn->rport));

  PRINTF("Created a server connection with remote address border router ");
  PRINT6ADDR(&border_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(border_conn->lport),
         UIP_HTONS(border_conn->rport));

  prepare_mcast();
  prepare_mcast_ch();

  if (join_mcast_group_ch() == NULL)
  {
    PRINTF("Failed to join multicast_ch group\n");
    PROCESS_EXIT();
  }

  ch_conn = udp_new(NULL, UIP_HTONS(0), NULL);
  udp_bind(ch_conn, UIP_HTONS(MCAST_SINK_UDP_PORT_CH));

  etimer_set(&et_ch, CLOCK_SECOND);
  etimer_set(&et, 10 * CLOCK_SECOND);

  while (1)
  {
    PROCESS_YIELD();
    if (ev == tcpip_event)
    {
      tcpip_handler();
    }
    if (etimer_expired(&et_ch))
    {
      PRINTF("Sending CH multicast for CH election\n");
      multicast_send_ch();
      etimer_set(&et_ch, CH_ELECTION_INTERVAL * CLOCK_SECOND);
    }
    if (etimer_expired(&et))
    {
      PRINTF("Sending multicast\n");
      multicast_send();
      etimer_set(&et, CH_MULTICAST_INTERVAL * CLOCK_SECOND);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
