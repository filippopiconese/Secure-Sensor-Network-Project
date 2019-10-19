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
#define UDP_CH_PORT 4444
#define MCAST_SINK_UDP_PORT 3001    /* Host byte order */
#define MCAST_SINK_UDP_PORT_CH 3002 /* Host byte order */

#define SERVER_REPLY 1
#define MAX_PAYLOAD_LEN 3

static struct uip_udp_conn *server_conn;
static struct uip_udp_conn *border_conn;
static struct uip_udp_conn *ch2ch_conn;
static struct uip_udp_conn *mcast_conn;
static struct uip_udp_conn *mcast_conn_ch;
static struct uip_udp_conn *ch_conn;

static uip_ipaddr_t border_ipaddr;
static uip_ipaddr_t ch_ipaddr;
static uip_ipaddr_t first_addr;

static char buf[MAX_PAYLOAD_LEN];

static int random_number;
static int ch_can_send = 1;
static int first_iteration = 0;
static int num_of_ch = 0;
static int count = 0;
static int tot = 0;
static int first_value = 0;

typedef struct ch_list
{
  int val;
  uip_ipaddr_t addr;
} ch_list_t;

static ch_list_t *ch_list_struct;

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
  char buf[5];

  PRINTF("Sending CH multicast data '%d' to ", random_number);
  PRINT6ADDR(&mcast_conn_ch->ripaddr);
  PRINTF("\n");

  itoa(random_number, buf, 10);
  uip_udp_packet_send(mcast_conn_ch, buf, strlen(buf));
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
  rss = cc2420_last_rssi;

  PRINTF("RSSI of Last Packet Received is %d dBm from ", rss);
  PRINT6ADDR(&originator_ipaddr);
  PRINTF("\n");

  return rss;
}

/*---------------------------------------------------------------------------*/

int digits_only(const char *s)
{
  while (*s)
  {
    if (isdigit(*s++) == 0)
      return 0;
  }

  return 1;
}

/*---------------------------------------------------------------------------*/

static void
tcpip_handler(void)
{
  char *appdata;

  if (uip_newdata())
  {
    appdata = (char *)uip_appdata;
    appdata[uip_datalen()] = '\0';

    if (strcmp(appdata, "A") == 0)
    {
      num_of_ch++;
      PRINTF("num_of_ch = %d\n", num_of_ch);
    }
    else if (digits_only(appdata))
    {
      int num = atoi(appdata);

      ch_list_struct = malloc(num_of_ch * sizeof(ch_list_t));
      if (count == 0)
      {
        first_value = num;
        first_addr = UIP_IP_BUF->srcipaddr;
      }
      else
      {
        ch_list_struct[count] = (ch_list_t){.val = num, .addr = UIP_IP_BUF->srcipaddr};
      }

      tot += num;

      if (count == num_of_ch - 1)
      {
        tot += random_number;

        int mean = tot / (num_of_ch + 1);

        PRINTF("Tot = %d ; Random number: %d ; Mean = %d\n", tot, random_number, mean);

        if (mean > random_number)
        {
          PRINTF("I am NOT the cluster head\n");
          ch_can_send = 0;
          int i;
          for (i = 0; i <= count; i++)
          {
            if (i == 0)
            {
              if (first_value >= mean)
              {
                ch_ipaddr = first_addr;
              }
            }
            if (ch_list_struct[i].val >= mean)
            {
              ch_ipaddr = ch_list_struct[i].addr;
            }
          }
        }
        else
        {
          PRINTF("I am the cluster head\n");
          ch_can_send = 1;
        }

        tot = 0;
        count = 0;
        free(ch_list_struct);
        ch_list_struct = NULL;
      }
      else
      {
        count++;
      }
    }
    else
    {
      uip_ipaddr_t client_ipaddr = UIP_IP_BUF->srcipaddr;
      PRINTF("DATA recv '%s' from ", appdata);
      PRINT6ADDR(&client_ipaddr);
      PRINTF("\n");

      if (ch_can_send)
      {
        // Try to redirect data to the border router
        send_packet(NULL, appdata, border_ipaddr, border_conn, UDP_BORDER_PORT);
      }
      else
      {
        PRINTF("Send packet to cluster head ");
        PRINT6ADDR(&ch_ipaddr);
        PRINTF("\n");
        send_packet(NULL, appdata, ch_ipaddr, ch2ch_conn, UDP_CH_PORT);
      }

      /* --------------------------------------------------- */
      // Send RSSI to client to regulate transmission power
      signed char rss = calculate_RSSI(client_ipaddr);
      char *rssi = malloc(4 * sizeof(char));
      snprintf(rssi, 4, "%d", rss);
      send_packet(NULL, rssi, client_ipaddr, server_conn, UDP_CLIENT_PORT);
      free(rssi);
      rssi = NULL;
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
  static struct etimer random_number_et;
  static struct etimer ch_election_et;
  static struct etimer multicast_et;

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
  // Connection to the client
  ch2ch_conn = udp_new(NULL, UIP_HTONS(UDP_CH_PORT), NULL);
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
  udp_bind(border_conn, UIP_HTONS(UDP_BORDER_PORT));
  udp_bind(ch2ch_conn, UIP_HTONS(UDP_CH_PORT));

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

  etimer_set(&random_number_et, 60 * CLOCK_SECOND);
  etimer_set(&ch_election_et, 90 * CLOCK_SECOND);
  etimer_set(&multicast_et, 120 * CLOCK_SECOND);
  while (1)
  {
    PROCESS_YIELD();
    if (ev == tcpip_event)
    {
      tcpip_handler();
    }
    if (etimer_expired(&random_number_et))
    {
      if (first_iteration == 0)
      {
        PRINTF("Sending A to ");
        PRINT6ADDR(&mcast_conn_ch->ripaddr);
        PRINTF("\n");
        uip_udp_packet_send(mcast_conn_ch, "A", strlen("A"));
        first_iteration++;
      }
      random_number = abs(rand() % 1000 + 1);

      etimer_set(&random_number_et, 150 * CLOCK_SECOND);
    }
    if (etimer_expired(&ch_election_et))
    {
      PRINTF("Sending CH multicast for CH election\n");
      multicast_send_ch();
      etimer_set(&ch_election_et, 150 * CLOCK_SECOND);
    }
    if (etimer_expired(&multicast_et))
    {
      PRINTF("Sending multicast to clients to let them select the best CH\n");
      multicast_send();
      etimer_set(&multicast_et, 150 * CLOCK_SECOND);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
