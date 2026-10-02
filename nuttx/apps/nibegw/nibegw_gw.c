/****************************************************************************
 * nibegw/nibegw_gw.c
 *
 * Copyright (c) 2010-2026 Contributors to the openHAB project
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * Gateway loop ported from nibegw.c by pauli.anttila@gmail.com: listens to
 * the heat pump on RS-485, acknowledges frames addressed to us, relays read
 * and write requests on token frames and forwards valid frames by UDP.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

#ifdef CONFIG_NIBEGW_WATCHDOG
#  include <nuttx/timers/watchdog.h>
#endif

#include "nibegw.h"
#include "nibegw_proto.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define POLL_TIMEOUT_MS  1000   /* Bound on how long a stop request waits */
#define REOPEN_DELAY_S   1
#define UDP_RETRY_S      5
#define MAX_UDP_MSG      64
#define STOP_TIMEOUT_MS  3000

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct nibegw_config_s g_cfg;
static struct nibegw_stats_s g_stats;
static volatile bool g_stop;
static volatile pid_t g_pid = -1;
static bool g_forward;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_frame(const char *tag, const uint8_t *frame, size_t len)
{
  size_t i;

  printf("%s:", tag);
  for (i = 0; i < len; i++)
    {
      printf(" %02X", frame[i]);
    }

  printf("\n");
}

#ifdef CONFIG_NIBEGW_WATCHDOG
/* The hardware watchdog is fed from the gateway loop, which runs at least
 * every POLL_TIMEOUT_MS even on a quiet bus, so it resets the board when
 * the gateway hangs as well as when the whole system does.
 */

static int watchdog_start(void)
{
  int fd;

  fd = open(CONFIG_NIBEGW_WATCHDOG_DEVPATH, O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_ERR, "nibegw: open %s: %d\n",
             CONFIG_NIBEGW_WATCHDOG_DEVPATH, -errno);
      return -1;
    }

  if (ioctl(fd, WDIOC_SETTIMEOUT, CONFIG_NIBEGW_WATCHDOG_TIMEOUT) < 0 ||
      ioctl(fd, WDIOC_START, 0) < 0)
    {
      syslog(LOG_ERR, "nibegw: watchdog start: %d\n", -errno);
      close(fd);
      return -1;
    }

  g_stats.watchdog = true;
  return fd;
}

static void watchdog_stop(int fd)
{
  if (fd >= 0)
    {
      ioctl(fd, WDIOC_STOP, 0);
      close(fd);
      g_stats.watchdog = false;
    }
}
#endif

static int serial_open(const char *device)
{
  struct termios tio;
  int ret;
  int fd;

  fd = open(device, O_RDWR | O_NOCTTY);
  if (fd < 0)
    {
      return -errno;
    }

  if (tcgetattr(fd, &tio) < 0)
    {
      goto errout;
    }

  /* 9600 baud, 8 data bits, no parity, 1 stop bit, no flow control.
   * Direction control of the RS-485 transceiver is done by the driver.
   */

  cfmakeraw(&tio);
  cfsetspeed(&tio, B9600);
  tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
  tio.c_cflag |= CS8 | CREAD | CLOCAL;

  if (tcsetattr(fd, TCSANOW, &tio) < 0)
    {
      goto errout;
    }

  return fd;

errout:
  ret = -errno;
  close(fd);
  return ret;
}

static int udp_open(uint16_t port)
{
  struct sockaddr_in addr;
  int ret;
  int fd;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      return -errno;
    }

  ret = fcntl(fd, F_GETFL, 0);
  if (ret < 0 || fcntl(fd, F_SETFL, ret | O_NONBLOCK) < 0)
    {
      goto errout;
    }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(port);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      goto errout;
    }

  return fd;

errout:
  ret = -errno;
  close(fd);
  return ret;
}

static void udp_open_all(int *rdfd, int *wrfd)
{
  if (*rdfd < 0)
    {
      *rdfd = udp_open(g_cfg.read_port);
      if (*rdfd < 0)
        {
          syslog(LOG_ERR, "nibegw: UDP port %u: %d\n",
                 g_cfg.read_port, *rdfd);
        }
    }

  if (*wrfd < 0)
    {
      *wrfd = udp_open(g_cfg.write_port);
      if (*wrfd < 0)
        {
          syslog(LOG_ERR, "nibegw: UDP port %u: %d\n",
                 g_cfg.write_port, *wrfd);
        }
    }
}

static void serial_write(int fd, const uint8_t *data, size_t len)
{
  if (write(fd, data, len) != (ssize_t)len)
    {
      g_stats.serial_errors++;
      return;
    }

  tcdrain(fd);
}

/* Relay a pending UDP request to the heat pump.  Returns true when a
 * request was sent, in which case it replaces the ACK.
 */

static bool forward_request(int udpfd, int serfd)
{
  uint8_t msg[MAX_UDP_MSG];
  ssize_t len;

  if (udpfd < 0)
    {
      return false;
    }

  len = recv(udpfd, msg, sizeof(msg), 0);
  if (len <= 0)
    {
      return false;
    }

  serial_write(serfd, msg, len);
  g_stats.requests_sent++;

  if (g_cfg.verbose > 1)
    {
      print_frame("request", msg, len);
    }

  return true;
}

static void send_byte(int serfd, uint8_t byte)
{
  serial_write(serfd, &byte, 1);
}

static void handle_frame(int serfd, int rdfd, int wrfd,
                         const struct sockaddr_in *dest,
                         const uint8_t *frame, size_t len)
{
  bool ours = NIBEGW_FRAME_ADDR(frame) == g_cfg.addr;
  bool sent = false;

  g_stats.frames_ok++;
  clock_gettime(CLOCK_MONOTONIC, &g_stats.last_frame);

  /* Answer first: the heat pump only waits a short time for the reply */

  if (ours || g_cfg.ackall)
    {
      if (NIBEGW_FRAME_LEN(frame) == 0 &&
          NIBEGW_FRAME_CMD(frame) == NIBEGW_CMD_READ_TOKEN)
        {
          g_stats.read_tokens++;
          sent = forward_request(rdfd, serfd);
        }
      else if (NIBEGW_FRAME_LEN(frame) == 0 &&
               NIBEGW_FRAME_CMD(frame) == NIBEGW_CMD_WRITE_TOKEN)
        {
          g_stats.write_tokens++;
          sent = forward_request(wrfd, serfd);
        }

      if (!sent && g_cfg.sendack)
        {
          send_byte(serfd, NIBEGW_ACK);
          g_stats.acks++;
        }
    }

  if (g_forward && (ours || g_cfg.sendall))
    {
      if (rdfd >= 0 &&
          sendto(rdfd, frame, len, 0, (const struct sockaddr *)dest,
                 sizeof(*dest)) == (ssize_t)len)
        {
          g_stats.udp_sent++;
        }
      else
        {
          g_stats.udp_errors++;
        }
    }

  if (g_cfg.verbose)
    {
      print_frame("frame", frame, len);
    }
}

static void handle_badcrc(int serfd, const uint8_t *frame, size_t len)
{
  g_stats.frames_badcrc++;

  if ((NIBEGW_FRAME_ADDR(frame) == g_cfg.addr || g_cfg.ackall) &&
      g_cfg.sendack)
    {
      send_byte(serfd, NIBEGW_NAK);
      g_stats.naks++;
    }

  if (g_cfg.verbose)
    {
      print_frame("badcrc", frame, len);
    }
}

static int nibegw_task(int argc, char *argv[])
{
  struct nibegw_framer_s framer;
  struct sockaddr_in dest;
  struct pollfd pfd;
  struct timespec now;
  time_t udp_retry = 0;
  uint8_t buf[64];
  ssize_t n;
  ssize_t i;
  size_t framelen;
  int serfd = -1;
  int rdfd = -1;
  int wrfd = -1;
#ifdef CONFIG_NIBEGW_WATCHDOG
  int wdfd = -1;
#endif
  int ret;

  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port   = htons(g_cfg.remote_port);

  g_forward = g_cfg.remote_host[0] != '\0';
  if (g_forward &&
      inet_pton(AF_INET, g_cfg.remote_host, &dest.sin_addr) != 1)
    {
      syslog(LOG_ERR, "nibegw: invalid remote host %s\n",
             g_cfg.remote_host);
      g_pid = -1;
      return 1;
    }

  if (g_forward)
    {
      syslog(LOG_INFO, "nibegw: %s, address 0x%02X, forwarding to %s:%u\n",
             g_cfg.device, g_cfg.addr, g_cfg.remote_host,
             g_cfg.remote_port);
    }
  else
    {
      syslog(LOG_WARNING, "nibegw: %s, address 0x%02X, no remote host set,"
             " not forwarding\n", g_cfg.device, g_cfg.addr);
    }

  nibegw_framer_reset(&framer);

#ifdef CONFIG_NIBEGW_WATCHDOG
  wdfd = watchdog_start();
#endif

  while (!g_stop)
    {
#ifdef CONFIG_NIBEGW_WATCHDOG
      if (wdfd >= 0)
        {
          ioctl(wdfd, WDIOC_KEEPALIVE, 0);
        }
#endif

      if (serfd < 0)
        {
          serfd = serial_open(g_cfg.device);
          if (serfd < 0)
            {
              syslog(LOG_ERR, "nibegw: open %s: %d\n", g_cfg.device, serfd);
              g_stats.serial_errors++;
              sleep(REOPEN_DELAY_S);
              continue;
            }

          nibegw_framer_reset(&framer);
        }

      clock_gettime(CLOCK_MONOTONIC, &now);
      if ((rdfd < 0 || wrfd < 0) && now.tv_sec >= udp_retry)
        {
          udp_open_all(&rdfd, &wrfd);
          udp_retry = now.tv_sec + UDP_RETRY_S;
        }

      pfd.fd      = serfd;
      pfd.events  = POLLIN;
      pfd.revents = 0;

      ret = poll(&pfd, 1, POLL_TIMEOUT_MS);
      if (ret == 0 || (ret < 0 && errno == EINTR))
        {
          continue;
        }

      n = ret < 0 ? -1 : read(serfd, buf, sizeof(buf));
      if (n <= 0)
        {
          if (n < 0 && (errno == EINTR || errno == EAGAIN))
            {
              continue;
            }

          syslog(LOG_ERR, "nibegw: read %s: %d\n", g_cfg.device,
                 n < 0 ? -errno : 0);
          g_stats.serial_errors++;
          close(serfd);
          serfd = -1;
          sleep(REOPEN_DELAY_S);
          continue;
        }

      for (i = 0; i < n; i++)
        {
          switch (nibegw_framer_push(&framer, buf[i], &framelen))
            {
              case NIBEGW_PUSH_FRAME:
                handle_frame(serfd, rdfd, wrfd, &dest, framer.buf,
                             framelen);
                break;

              case NIBEGW_PUSH_BADCRC:
                handle_badcrc(serfd, framer.buf, framelen);
                break;

              case NIBEGW_PUSH_JUNK:
                g_stats.junk_bytes++;
                break;

              default:
                break;
            }
        }
    }

  if (serfd >= 0)
    {
      close(serfd);
    }

  if (rdfd >= 0)
    {
      close(rdfd);
    }

  if (wrfd >= 0)
    {
      close(wrfd);
    }

  /* A deliberate stop must not reset the board */

#ifdef CONFIG_NIBEGW_WATCHDOG
  watchdog_stop(wdfd);
#endif

  syslog(LOG_INFO, "nibegw: stopped\n");
  g_pid = -1;
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void nibegw_config_default(struct nibegw_config_s *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  strlcpy(cfg->device, CONFIG_NIBEGW_DEVICE, sizeof(cfg->device));
  cfg->remote_port = CONFIG_NIBEGW_REMOTE_PORT;
  cfg->read_port   = CONFIG_NIBEGW_READ_PORT;
  cfg->write_port  = CONFIG_NIBEGW_WRITE_PORT;
  cfg->addr        = CONFIG_NIBEGW_RS485_ADDR;
  cfg->sendack     = true;
}

bool nibegw_running(void)
{
  pid_t pid = g_pid;

  return pid > 0 && kill(pid, 0) == 0;
}

int nibegw_start(const struct nibegw_config_s *cfg)
{
  int pid;

  if (nibegw_running())
    {
      return -EBUSY;
    }

  g_cfg = *cfg;
  memset(&g_stats, 0, sizeof(g_stats));
  clock_gettime(CLOCK_MONOTONIC, &g_stats.started);
  g_stop = false;

  pid = task_create("nibegw_gw", CONFIG_NIBEGW_PRIORITY,
                    CONFIG_NIBEGW_STACKSIZE, nibegw_task, NULL);
  if (pid < 0)
    {
      return -errno;
    }

  g_pid = pid;
  return pid;
}

int nibegw_stop(void)
{
  int waited;

  if (!nibegw_running())
    {
      return -ESRCH;
    }

  g_stop = true;

  for (waited = 0; waited < STOP_TIMEOUT_MS; waited += 100)
    {
      if (!nibegw_running())
        {
          return 0;
        }

      usleep(100 * 1000);
    }

  return -ETIMEDOUT;
}

const struct nibegw_config_s *nibegw_config(void)
{
  return &g_cfg;
}

const struct nibegw_stats_s *nibegw_stats(void)
{
  return &g_stats;
}
