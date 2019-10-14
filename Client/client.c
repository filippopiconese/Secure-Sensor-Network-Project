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

#define UDP_EXAMPLE_ID  190

#define DEBUG DEBUG_FULL
#include "net/ipv6/uip-debug.h"

#ifndef PERIOD
#define PERIOD 10
#endif

#define START_INTERVAL (15 * CLOCK_SECOND)
#define SEND_INTERVAL (PERIOD * CLOCK_SECOND)
#define SEND_TIME (2 * CLOCK_SECOND)
#define MAX_PAYLOAD_LEN 100


static struct uip_udp_conn *client_conn;
static uip_ipaddr_t server_ipaddr;

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client process");
AUTOSTART_PROCESSES(&udp_client_process);
/*---------------------------------------------------------------------------*/
static uint8_t transmission_power = 31;
static bool lock_transmission_power = 0; // 1 if optimal tPower is reached, 0 otherwise

static void
adjust_transmission_power(char* rssi) {
  uint16_t rssi_int = atoi(rssi);
  PRINTF("The RSSI received from cluster node is %d dBm. TPower is %d\n", rssi_int, transmission_power);
  if(rssi_int >= -70 && lock_transmission_power != 1 && transmission_power > 0) {
    transmission_power--;
    PRINTF("Lowering TPower to %d\n", transmission_power);
  } else if(rssi_int < -70){
    lock_transmission_power = 1;
    if(transmission_power < 31) {
      transmission_power++;
      PRINTF("Increasing TPower to %d\n", transmission_power);
    } else {
      PRINTF("Nothing to improve. Transmission power is already at max value!\n");
    }
  } else if(lock_transmission_power){
    PRINTF("Optimal TPower reached\n");
  } 
  cc2420_set_txpower(transmission_power);
}

static void
tcpip_handler(void)
{
    char *str;

    if(uip_newdata()) {
        str = uip_appdata;
        str[uip_datalen()] = '\0';
        adjust_transmission_power(str);
    }
}
/*---------------------------------------------------------------------------*/
static void
send_packet(void *ptr)
{
    char buf[MAX_PAYLOAD_LEN];

    sprintf(buf, "Hello world");
    PRINTF("Sending data '%s' to ", buf);
    PRINT6ADDR(&server_ipaddr);
    PRINTF("\n");
    uip_udp_packet_sendto(client_conn, buf, strlen(buf), &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));
}
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Client IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
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
//Set address of the cluster head
static void
set_global_address(void)
{
  uip_ip6addr(&server_ipaddr, 0xfe80, 0, 0, 0, 0xc30c, 0, 0, 0x0009); //fe80::c30c:0:0:9
}
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer periodic;
  static struct ctimer backoff_timer;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  set_global_address();

  print_local_addresses();

  /* new connection with remote host */
  client_conn = udp_new(&server_ipaddr, UIP_HTONS(UDP_SERVER_PORT), NULL); 
  if(client_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT)); 

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
	UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

  etimer_set(&periodic, SEND_INTERVAL);

  while(1) {  
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }

    if (etimer_expired(&periodic)) {
      etimer_reset(&periodic);
      ctimer_set(&backoff_timer, SEND_TIME, send_packet, NULL);
    } 
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
