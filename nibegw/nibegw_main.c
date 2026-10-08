/****************************************************************************
 * nibegw/nibegw_main.c
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * NSH command to control the gateway task: start, stop, status and the
 * settings stored in flash.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "nibegw.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void usage(const char *progname)
{
  fprintf(stderr,
          "Usage:\n"
          "  %s [status]               Show gateway state and counters\n"
          "  %s start [options]        Start the gateway\n"
          "  %s stop                   Stop the gateway\n"
          "  %s restart                Restart with the saved settings\n"
          "  %s config                 Show the saved settings\n"
          "  %s set <key> <value>      Save a setting and restart\n"
          "\n"
          "Keys: host (IP to forward to, \"none\" to stop forwarding),\n"
          "      port, readport, writeport, addr\n"
          "\n"
          "Start options override the saved settings until next start:\n"
          "  -d <device>   Serial device (default "
          CONFIG_NIBEGW_DEVICE ")\n"
          "  -a <ip>       Remote host\n"
          "  -p <port>     Remote UDP port\n"
          "  -l <port>     Local UDP port for read requests\n"
          "  -w <port>     Local UDP port for write requests\n"
          "  -r <addr>     RS-485 address to answer for\n"
          "  -i            Forward frames for all addresses\n"
          "  -n            Don't send ACK/NAK at all\n"
          "  -o            ACK frames for all addresses\n"
          "  -v            Print frames (repeat for more)\n",
          progname, progname, progname, progname, progname, progname);
}

static void print_config(const struct nibegw_config_s *cfg)
{
  printf("device     %s\n", cfg->device);
  printf("host       %s\n", cfg->remote_host[0] != '\0' ?
                            cfg->remote_host : "(not set, not forwarding)");
  printf("port       %u\n", cfg->remote_port);
  printf("readport   %u\n", cfg->read_port);
  printf("writeport  %u\n", cfg->write_port);
  printf("addr       0x%02X\n", cfg->addr);
}

static void print_duration(const char *label, time_t secs)
{
  printf("%s %ldh%02ldm%02lds\n", label, (long)(secs / 3600),
         (long)(secs / 60 % 60), (long)(secs % 60));
}

static int cmd_status(void)
{
  const struct nibegw_config_s *cfg = nibegw_config();
  const struct nibegw_stats_s *st = nibegw_stats();
  struct timespec now;

  if (!nibegw_running())
    {
      printf("nibegw: not running\n");
      return 1;
    }

  clock_gettime(CLOCK_MONOTONIC, &now);

  printf("nibegw: running on %s, address 0x%02X\n", cfg->device, cfg->addr);
  if (cfg->remote_host[0] != '\0')
    {
      printf("forwarding to %s:%u\n", cfg->remote_host, cfg->remote_port);
    }
  else
    {
      printf("not forwarding (no host set)\n");
    }

  print_duration("uptime    ", now.tv_sec - st->started.tv_sec);
  printf("frames     ok %lu, bad crc %lu, junk bytes %lu\n",
         (unsigned long)st->frames_ok, (unsigned long)st->frames_badcrc,
         (unsigned long)st->junk_bytes);
  printf("replies    ack %lu, nak %lu\n",
         (unsigned long)st->acks, (unsigned long)st->naks);
  printf("tokens     read %lu, write %lu, requests sent %lu\n",
         (unsigned long)st->read_tokens, (unsigned long)st->write_tokens,
         (unsigned long)st->requests_sent);
  printf("udp        sent %lu, errors %lu\n",
         (unsigned long)st->udp_sent, (unsigned long)st->udp_errors);
  printf("serial     errors %lu\n", (unsigned long)st->serial_errors);
  printf("watchdog   %s\n", st->watchdog ? "on" : "off");

  if (st->last_frame.tv_sec != 0)
    {
      print_duration("last frame", now.tv_sec - st->last_frame.tv_sec);
    }
  else
    {
      printf("last frame never\n");
    }

  return 0;
}

static int start_saved(void)
{
  struct nibegw_config_s cfg;
  int ret;

  nibegw_config_default(&cfg);
  nibegw_conf_load(&cfg);

  ret = nibegw_start(&cfg);
  if (ret < 0)
    {
      fprintf(stderr, "nibegw: start failed: %d\n", ret);
      return 1;
    }

  return 0;
}

static int cmd_start(int argc, char *argv[])
{
  struct nibegw_config_s cfg;
  int ret;
  int c;

  nibegw_config_default(&cfg);
  nibegw_conf_load(&cfg);

  optind = 1;
  while ((c = getopt(argc, argv, "d:a:p:l:w:r:inov")) != -1)
    {
      ret = 0;
      switch (c)
        {
          case 'd':
            strlcpy(cfg.device, optarg, sizeof(cfg.device));
            break;

          case 'a':
            ret = nibegw_conf_apply(&cfg, "host", optarg);
            break;

          case 'p':
            ret = nibegw_conf_apply(&cfg, "port", optarg);
            break;

          case 'l':
            ret = nibegw_conf_apply(&cfg, "readport", optarg);
            break;

          case 'w':
            ret = nibegw_conf_apply(&cfg, "writeport", optarg);
            break;

          case 'r':
            ret = nibegw_conf_apply(&cfg, "addr", optarg);
            break;

          case 'i':
            cfg.sendall = true;
            break;

          case 'n':
            cfg.sendack = false;
            break;

          case 'o':
            cfg.ackall = true;
            break;

          case 'v':
            cfg.verbose++;
            break;

          default:
            usage("nibegw");
            return 1;
        }

      if (ret < 0)
        {
          fprintf(stderr, "nibegw: invalid value for -%c: %s\n", c, optarg);
          return 1;
        }
    }

  ret = nibegw_start(&cfg);
  if (ret == -EBUSY)
    {
      fprintf(stderr, "nibegw: already running\n");
      return 1;
    }
  else if (ret < 0)
    {
      fprintf(stderr, "nibegw: start failed: %d\n", ret);
      return 1;
    }

  return 0;
}

static int cmd_stop(void)
{
  int ret = nibegw_stop();

  if (ret == -ESRCH)
    {
      fprintf(stderr, "nibegw: not running\n");
      return 1;
    }
  else if (ret < 0)
    {
      fprintf(stderr, "nibegw: stop failed: %d\n", ret);
      return 1;
    }

  return 0;
}

static int cmd_set(const char *key, const char *value)
{
  struct nibegw_config_s cfg;
  int ret;

  if (strcmp(key, "host") == 0 && strcmp(value, "none") == 0)
    {
      value = "";
    }

  nibegw_config_default(&cfg);
  nibegw_conf_load(&cfg);

  ret = nibegw_conf_apply(&cfg, key, value);
  if (ret == -ENOENT)
    {
      fprintf(stderr, "nibegw: unknown setting %s\n", key);
      return 1;
    }
  else if (ret < 0)
    {
      fprintf(stderr, "nibegw: invalid value for %s: %s\n", key, value);
      return 1;
    }

  ret = nibegw_conf_save(&cfg);
  if (ret < 0)
    {
      fprintf(stderr, "nibegw: saving " CONFIG_NIBEGW_CONFIG_PATH
              " failed: %d\n", ret);
      return 1;
    }

  if (nibegw_running())
    {
      nibegw_stop();
      return start_saved();
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct nibegw_config_s cfg;

  if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
      return cmd_status();
    }

  if (strcmp(argv[1], "start") == 0)
    {
      return cmd_start(argc - 1, &argv[1]);
    }

  if (strcmp(argv[1], "stop") == 0)
    {
      return cmd_stop();
    }

  if (strcmp(argv[1], "restart") == 0)
    {
      if (nibegw_running())
        {
          nibegw_stop();
        }

      return start_saved();
    }

  if (strcmp(argv[1], "config") == 0)
    {
      nibegw_config_default(&cfg);
      if (nibegw_conf_load(&cfg) < 0)
        {
          printf("(no " CONFIG_NIBEGW_CONFIG_PATH ", using defaults)\n");
        }

      print_config(&cfg);
      return 0;
    }

  if (strcmp(argv[1], "set") == 0 && argc == 4)
    {
      return cmd_set(argv[2], argv[3]);
    }

  usage(argv[0]);
  return 1;
}
