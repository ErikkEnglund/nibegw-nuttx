/****************************************************************************
 * nibegw/nibegw_net.c
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * Network watchdog.  netinit brings Wi-Fi up once at boot; if the access
 * point is not there yet (after a power cut the router often boots slower
 * than the gateway) the board would stay without an address.  This task
 * retries with the saved Wi-Fi settings until it has one.  It also renews
 * the DHCP lease, which netinit never does.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sched.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "netutils/dhcpc.h"
#include "netutils/netlib.h"
#include "wireless/wapi.h"

#include "nibegw.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NET_IFNAME        "wlan0"
#define NET_CHECK_S       30
#define NET_FIRST_CHECK_S 60    /* Leave the first attempt to netinit */
#define NET_RENEW_RETRY_S 60
#define NET_RENEW_MIN_S   60

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool has_address(void)
{
  struct in_addr addr;

  return netlib_get_ipv4addr(NET_IFNAME, &addr) == 0 &&
         addr.s_addr != INADDR_ANY;
}

/* Run DHCP and apply the result.  On success *renew is the number of
 * seconds until the lease should be renewed.
 */

static int dhcp_obtain(uint32_t *renew)
{
  struct dhcpc_state ds;
  uint8_t mac[IFHWADDRLEN];
  void *handle;
  int ret;

  ret = netlib_getmacaddr(NET_IFNAME, mac);
  if (ret < 0)
    {
      return ret;
    }

  handle = dhcpc_open(NET_IFNAME, mac, IFHWADDRLEN);
  if (handle == NULL)
    {
      return -EINVAL;
    }

  ret = dhcpc_request(handle, &ds);
  dhcpc_close(handle);
  if (ret < 0)
    {
      return ret;
    }

  /* DNS servers are left as netinit set them; the gateway does not use
   * name lookups.
   */

  netlib_set_ipv4addr(NET_IFNAME, &ds.ipaddr);
  if (ds.netmask.s_addr != 0)
    {
      netlib_set_ipv4netmask(NET_IFNAME, &ds.netmask);
    }

  if (ds.default_router.s_addr != 0)
    {
      netlib_set_dripv4addr(NET_IFNAME, &ds.default_router);
    }

  *renew = ds.renewal_time != 0 ? ds.renewal_time : ds.lease_time / 2;
  if (*renew < NET_RENEW_MIN_S)
    {
      *renew = NET_RENEW_MIN_S;
    }

  return 0;
}

/* Associate again with the saved settings.  Returns false when no Wi-Fi
 * settings are saved, in which case there is nothing to retry.
 */

static bool associate(void)
{
  struct wpa_wconfig_s conf;
  void *load;
  int sockfd;

  load = wapi_load_config(NET_IFNAME, NULL, &conf);
  if (load == NULL)
    {
      return false;
    }

  if (conf.ssidlen > 0)
    {
      /* After a failed attempt the driver keeps retrying the old
       * connection and refuses new settings until it is disconnected.
       */

      sockfd = wapi_make_socket();
      if (sockfd >= 0)
        {
          wpa_driver_wext_disconnect(sockfd, NET_IFNAME);
          close(sockfd);
        }

      wpa_driver_wext_associate(&conf);
    }

  wapi_unload_config(load);
  return conf.ssidlen > 0;
}

static int nibegw_net_task(int argc, char *argv[])
{
  struct timespec now;
  time_t renew_at = 0;    /* Lease time unknown: renew at the first check */
  uint32_t renew;
  bool lost = false;
  uint8_t flags;

  sleep(NET_FIRST_CHECK_S);

  for (; ; )
    {
      clock_gettime(CLOCK_MONOTONIC, &now);

      if (has_address())
        {
          if (lost)
            {
              syslog(LOG_INFO, "nibegw: network is back\n");
              lost = false;
            }

          if (now.tv_sec >= renew_at)
            {
              if (dhcp_obtain(&renew) == 0)
                {
                  syslog(LOG_INFO, "nibegw: DHCP lease renewed, next in "
                         "%lu s\n", (unsigned long)renew);
                  renew_at = now.tv_sec + renew;
                }
              else
                {
                  syslog(LOG_WARNING, "nibegw: DHCP renewal failed\n");
                  renew_at = now.tv_sec + NET_RENEW_RETRY_S;
                }
            }
        }
      else
        {
          if (!lost)
            {
              syslog(LOG_WARNING, "nibegw: no IP address, retrying Wi-Fi\n");
              lost = true;
            }

          /* The Wi-Fi driver reconnects by itself once it has been
           * connected; only associate when there is no link at all.
           */

          if (netlib_getifstatus(NET_IFNAME, &flags) < 0 ||
              (flags & IFF_RUNNING) == 0)
            {
              netlib_ifup(NET_IFNAME);
              if (associate())
                {
                  sleep(5);
                }
            }

          if (dhcp_obtain(&renew) == 0)
            {
              renew_at = now.tv_sec + renew;
            }
        }

      sleep(NET_CHECK_S);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int nibegw_net_start(void)
{
  return task_create("nibegw_net", SCHED_PRIORITY_DEFAULT - 10, 4096,
                     nibegw_net_task, NULL);
}
