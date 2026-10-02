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
 * retries with the saved Wi-Fi settings until it has one.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <net/if.h>
#include <netinet/in.h>
#include <sched.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>

#include "netutils/netlib.h"
#include "wireless/wapi.h"

#include "nibegw.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NET_IFNAME        "wlan0"
#define NET_CHECK_S       30
#define NET_FIRST_CHECK_S 60    /* Leave the first attempt to netinit */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool has_address(void)
{
  struct in_addr addr;

  return netlib_get_ipv4addr(NET_IFNAME, &addr) == 0 &&
         addr.s_addr != INADDR_ANY;
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
  bool lost = false;
  uint8_t flags;

  sleep(NET_FIRST_CHECK_S);

  for (; ; )
    {
      if (has_address())
        {
          if (lost)
            {
              syslog(LOG_INFO, "nibegw: network is back\n");
              lost = false;
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

          netlib_obtain_ipv4addr(NET_IFNAME);
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
