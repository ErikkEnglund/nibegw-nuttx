/****************************************************************************
 * nibegw/nibegw_boot.c
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * Init entry point (CONFIG_INIT_ENTRYPOINT="nibegwboot_main"): brings up
 * NSH, which also starts networking and telnet, starts the gateway with the
 * saved settings and then runs the NSH console.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sched.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "nshlib/nshlib.h"

#include "nibegw.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct nibegw_config_s cfg;
  struct sched_param param;
  int ret;

#ifdef CONFIG_SYSTEM_NSH_PRIORITY
  /* Run the console at the same priority as nsh_main would */

  sched_getparam(0, &param);
  if (param.sched_priority != CONFIG_SYSTEM_NSH_PRIORITY)
    {
      param.sched_priority = CONFIG_SYSTEM_NSH_PRIORITY;
      sched_setparam(0, &param);
    }
#endif

  /* Networking (netinit) and telnet come up from here.  With
   * CONFIG_NETINIT_THREAD this does not wait for Wi-Fi.
   */

  nsh_initialize();

  nibegw_config_default(&cfg);
  nibegw_conf_load(&cfg);

  ret = nibegw_start(&cfg);
  if (ret < 0)
    {
      syslog(LOG_ERR, "nibegw: start failed: %d\n", ret);
    }

  ret = nibegw_net_start();
  if (ret < 0)
    {
      syslog(LOG_ERR, "nibegw: network watchdog failed: %d\n", ret);
    }

  ret = nsh_consolemain(argc, argv);

  /* nsh_consolemain() does not return */

  dprintf(STDERR_FILENO, "ERROR: nsh_consolemain() returned: %d\n", ret);
  return 1;
}
