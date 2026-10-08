/****************************************************************************
 * nibegw/nibegw.h
 *
 * Copyright (c) 2010-2026 Contributors to the openHAB project
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 ****************************************************************************/

#ifndef __NIBEGW_NIBEGW_H
#define __NIBEGW_NIBEGW_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct nibegw_config_s
{
  char     device[32];
  char     remote_host[16];
  uint16_t remote_port;
  uint16_t read_port;      /* Local UDP port for read requests */
  uint16_t write_port;     /* Local UDP port for write requests */
  uint8_t  addr;           /* RS-485 address to answer for */
  bool     sendall;        /* Forward frames for all addresses */
  bool     sendack;        /* Send ACK/NAK at all */
  bool     ackall;         /* ACK frames for all addresses */
  int      verbose;
};

struct nibegw_stats_s
{
  uint32_t frames_ok;
  uint32_t frames_badcrc;
  uint32_t junk_bytes;
  uint32_t acks;
  uint32_t naks;
  uint32_t read_tokens;
  uint32_t write_tokens;
  uint32_t requests_sent;  /* UDP requests forwarded to the heat pump */
  uint32_t udp_sent;
  uint32_t udp_errors;
  uint32_t serial_errors;
  bool     watchdog;       /* Hardware watchdog running */
  struct timespec started;
  struct timespec last_frame;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void nibegw_config_default(struct nibegw_config_s *cfg);

/* Persistent settings in CONFIG_NIBEGW_CONFIG_PATH, one "key=value" per
 * line.  Keys: host, port, readport, writeport, addr.
 */

int nibegw_conf_load(struct nibegw_config_s *cfg);
int nibegw_conf_save(const struct nibegw_config_s *cfg);
int nibegw_conf_apply(struct nibegw_config_s *cfg, const char *key,
                      const char *value);

/* Start the gateway task with a copy of cfg.  Returns the task PID or a
 * negated errno value.
 */

int nibegw_start(const struct nibegw_config_s *cfg);

/* Ask the gateway task to exit and wait for it.  Returns 0 or a negated
 * errno value.
 */

int nibegw_stop(void);

bool nibegw_running(void);

const struct nibegw_config_s *nibegw_config(void);
const struct nibegw_stats_s *nibegw_stats(void);

/* Start the task that retries Wi-Fi and DHCP while there is no address */

int nibegw_net_start(void);

#endif /* __NIBEGW_NIBEGW_H */
