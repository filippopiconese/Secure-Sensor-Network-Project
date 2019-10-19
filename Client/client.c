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
#include "sys/ctimer.h"
#include "net/ipv6/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uip-udp-packet.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "dev/serial-line.h"
#include "net/ipv6/uip-ds6-route.h"

#include "sys/log.h"

#include "cc2420.h"

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 6666

#define DEBUG DEBUG_FULL
#include "net/ipv6/uip-debug.h"

#define MCAST_SINK_UDP_PORT 3001 /* Host byte order */

#ifndef PERIOD
#define PERIOD 31
#endif

#define SETUP_INTERVAL (150 * CLOCK_SECOND)
#define SEND_INTERVAL (PERIOD * CLOCK_SECOND)
#define SEND_TIME (2 * CLOCK_SECOND)
#define MAX_PAYLOAD_LEN 100

static struct uip_udp_conn *client_conn;
static struct uip_udp_conn *sink_conn;
static uip_ipaddr_t ch_ipaddr;

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client process");
AUTOSTART_PROCESSES(&udp_client_process);
/*---------------------------------------------------------------------------*/
static uint8_t transmission_power = 31;
static int16_t best_rssi = -200; // Set to a minimum so it will be changed from the first packet received by CH

/*---------------------------------------------------------------------------*/
static signed char
calculate_RSSI(uip_ipaddr_t originator_ipaddr)
{
  static signed char rss;
  rss = cc2420_last_rssi;

  PRINTF("RSSI of Last Packet Received is %d dBm from ", rss);
  PRINT6ADDR(&originator_ipaddr);
  PRINTF("\n");

  return rss;
}

/*---------------------------------------------------------------------------*/
static uip_ds6_maddr_t *
join_mcast_group(void)
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
static void
adjust_transmission_power(char *rssi)
{
  uint16_t rssi_int = atoi(rssi);

  PRINTF("The RSSI received from cluster node is %d dBm. TPower is %d\n", rssi_int, transmission_power);

  if (rssi_int >= -65 && transmission_power > 2)
  {
    transmission_power -= 2;
    PRINTF("Lowering TPower to %d\n", transmission_power);
    cc2420_set_txpower(transmission_power);
  }
  else if (rssi_int < -70)
  {
    if (transmission_power < 31)
    {
      transmission_power++;
      PRINTF("Increasing TPower to %d\n", transmission_power);
      cc2420_set_txpower(transmission_power);
    }
    else
    {
      PRINTF("The TPower is already at max value!\n");
    }
  }
  else
  {
    PRINTF("Optimal TPower reached\n");
  }
}

/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  char *str;

  if (uip_newdata())
  {
    str = uip_appdata;

    if (strlen(str) < 3)
    {
      signed char rssi_tmp = calculate_RSSI(UIP_IP_BUF->srcipaddr);
      if (rssi_tmp > best_rssi)
      {
        best_rssi = rssi_tmp;
        ch_ipaddr = UIP_IP_BUF->srcipaddr;
        PRINTF("New CH is ");
        PRINT6ADDR(&ch_ipaddr);
        PRINTF("\n");
      }
      else
      {
        PRINTF("Best CH already set\n");
      }
    }
    else
    {
      str[uip_datalen()] = '\0';
      adjust_transmission_power(str);
    }
  }

  str = NULL;
}

/*---------------------------------------------------------------------------*/
static void
send_packet(void *ptr)
{
  char buf[MAX_PAYLOAD_LEN];

  sprintf(buf, "Hello world");
  PRINTF("Sending data '%s' to ", buf);
  PRINT6ADDR(&ch_ipaddr);
  PRINTF("\n");
  uip_udp_packet_sendto(client_conn, buf, strlen(buf), &ch_ipaddr, UIP_HTONS(UDP_SERVER_PORT));
}

/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Client IPv6 addresses: ");
  for (i = 0; i < UIP_DS6_ADDR_NB; i++)
  {
    state = uip_ds6_if.addr_list[i].state;
    if (uip_ds6_if.addr_list[i].isused &&
        (state == ADDR_TENTATIVE || state == ADDR_PREFERRED))
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
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer periodic;
  static struct ctimer backoff_timer;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  print_local_addresses();

  client_conn = udp_new(NULL, UIP_HTONS(UDP_SERVER_PORT), NULL);
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT));

  if (join_mcast_group() == NULL)
  {
    PRINTF("Failed to join multicast group\n");
    PROCESS_EXIT();
  }

  sink_conn = udp_new(NULL, UIP_HTONS(0), NULL);
  udp_bind(sink_conn, UIP_HTONS(MCAST_SINK_UDP_PORT));

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
         UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

  etimer_set(&periodic, SETUP_INTERVAL);

  while (1)
  {
    PROCESS_YIELD();
    if (ev == tcpip_event)
    {
      tcpip_handler();
    }

    if (etimer_expired(&periodic))
    {
      etimer_set(&periodic, SEND_INTERVAL);
      ctimer_set(&backoff_timer, SEND_TIME, send_packet, NULL);
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
