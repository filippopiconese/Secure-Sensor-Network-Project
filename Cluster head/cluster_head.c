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

#include "cc2420.h"

#define DEBUG DEBUG_PRINT
#include "net/ipv6/uip-debug.h"

#ifndef UIP_IP_BUF
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#endif

#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	6666
#define UDP_SERVER1_PORT 8888
#define UDP_BORDER_PORT	7777

#define SERVER_REPLY 1
#define MAX_PAYLOAD_LEN 100

#define UDP_EXAMPLE_ID  190

static struct uip_udp_conn *server_conn;
static struct uip_udp_conn *border_conn;

static uip_ipaddr_t border_ipaddr;

PROCESS(udp_server_process, "UDP server process");
AUTOSTART_PROCESSES(&udp_server_process);

/*---------------------------------------------------------------------------*/
static void
send_packet(void *ptr, char* buf, uip_ipaddr_t dest_ipaddr, struct uip_udp_conn *dest_conn, int rem_port)
{
    PRINTF("Sending data '%s'  ", buf);
    PRINT6ADDR(&dest_ipaddr);
    PRINTF("\n");
    uip_udp_packet_sendto(dest_conn, buf, strlen(buf), &dest_ipaddr, UIP_HTONS(rem_port));
}
/*---------------------------------------------------------------------------*/
static signed char
calculate_RSSI(uip_ipaddr_t originator_ipaddr) {
  static signed char rss;
  static signed char rss_val;
  static signed char rss_offset;
  rss_val = cc2420_last_rssi;
  rss_offset=0;
  rss=rss_val + rss_offset;
  PRINTF("RSSI of Last Packet Received is %d dBm from ",rss);
  PRINT6ADDR(&originator_ipaddr);

  // Calculate LQI
  radio_value_t* lqi = malloc(sizeof(radio_value_t));
  cc2420_driver.get_value(RADIO_PARAM_LAST_LINK_QUALITY, lqi);

  PRINTF("\nLQI = %d\n", *lqi);

  return rss;
}

/*---------------------------------------------------------------------------*/

static void
tcpip_handler(void)
{
  char *appdata;

  if(uip_newdata()) {
    appdata = (char *)uip_appdata;
    appdata[uip_datalen()] = 0;
    uip_ipaddr_t client_ipaddr = UIP_IP_BUF->srcipaddr;
    PRINTF("DATA recv '%s' from ", appdata);
    PRINT6ADDR(&client_ipaddr);       
    PRINTF("\n");
    
    signed char rss = calculate_RSSI(UIP_IP_BUF->srcipaddr);
    // Try to redirect data to the border router
    send_packet(NULL, appdata, border_ipaddr, border_conn, UDP_BORDER_PORT);
    /* --------------------------------------------------- */
    // Send RSSI to client
    char * rssi = malloc(4*sizeof(char));
    snprintf(rssi, 4, "%d", rss);
    send_packet(NULL, rssi, client_ipaddr, server_conn, UDP_CLIENT_PORT);
  } else {
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
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(state == ADDR_TENTATIVE || state == ADDR_PREFERRED) {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE) {
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
  if(server_conn == NULL) {
    PRINTF("No UDP connection available with the client, exiting the process!\n");
    PROCESS_EXIT();
  }

  if(border_conn == NULL) {
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

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    } 
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
