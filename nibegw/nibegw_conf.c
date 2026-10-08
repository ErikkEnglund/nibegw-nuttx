/****************************************************************************
 * nibegw/nibegw_conf.c
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * Gateway settings stored in flash, so the firmware carries no site
 * specific values.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nibegw.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int parse_uint(const char *value, unsigned long min,
                      unsigned long max, unsigned long *result)
{
  char *end;
  unsigned long v;

  errno = 0;
  v = strtoul(value, &end, 0);
  if (errno != 0 || end == value || *end != '\0' || v < min || v > max)
    {
      return -EINVAL;
    }

  *result = v;
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int nibegw_conf_apply(struct nibegw_config_s *cfg, const char *key,
                      const char *value)
{
  struct in_addr addr;
  uint16_t *port;
  unsigned long v;
  int ret;

  if (strcmp(key, "host") == 0)
    {
      /* An empty host disables forwarding */

      if (value[0] != '\0' && inet_pton(AF_INET, value, &addr) != 1)
        {
          return -EINVAL;
        }

      strlcpy(cfg->remote_host, value, sizeof(cfg->remote_host));
      return 0;
    }

  if (strcmp(key, "addr") == 0)
    {
      ret = parse_uint(value, 1, 0xff, &v);
      if (ret == 0)
        {
          cfg->addr = (uint8_t)v;
        }

      return ret;
    }

  if (strcmp(key, "port") == 0)
    {
      port = &cfg->remote_port;
    }
  else if (strcmp(key, "readport") == 0)
    {
      port = &cfg->read_port;
    }
  else if (strcmp(key, "writeport") == 0)
    {
      port = &cfg->write_port;
    }
  else
    {
      return -ENOENT;
    }

  ret = parse_uint(value, 1, 65535, &v);
  if (ret == 0)
    {
      *port = (uint16_t)v;
    }

  return ret;
}

int nibegw_conf_load(struct nibegw_config_s *cfg)
{
  char line[64];
  char *value;
  char *end;
  FILE *fp;

  /* Fall back to the temporary file if a power cut hit nibegw_conf_save()
   * between removing the old file and renaming the new one.
   */

  fp = fopen(CONFIG_NIBEGW_CONFIG_PATH, "r");
  if (fp == NULL)
    {
      fp = fopen(CONFIG_NIBEGW_CONFIG_PATH ".tmp", "r");
    }

  if (fp == NULL)
    {
      return -errno;
    }

  while (fgets(line, sizeof(line), fp) != NULL)
    {
      end = line + strcspn(line, "\r\n");
      *end = '\0';

      value = strchr(line, '=');
      if (line[0] == '#' || value == NULL)
        {
          continue;
        }

      *value++ = '\0';

      /* Ignore bad entries rather than refusing to start the gateway */

      nibegw_conf_apply(cfg, line, value);
    }

  fclose(fp);
  return 0;
}

int nibegw_conf_save(const struct nibegw_config_s *cfg)
{
  const char *tmp = CONFIG_NIBEGW_CONFIG_PATH ".tmp";
  FILE *fp;
  int ret;

  fp = fopen(tmp, "w");
  if (fp == NULL)
    {
      return -errno;
    }

  fprintf(fp, "host=%s\n", cfg->remote_host);
  fprintf(fp, "port=%u\n", cfg->remote_port);
  fprintf(fp, "readport=%u\n", cfg->read_port);
  fprintf(fp, "writeport=%u\n", cfg->write_port);
  fprintf(fp, "addr=0x%02x\n", cfg->addr);

  if (fclose(fp) != 0)
    {
      ret = -errno;
      remove(tmp);
      return ret;
    }

  /* The new file is complete before the old one goes away.  SPIFFS cannot
   * rename onto an existing file, hence the remove() first.
   */

  remove(CONFIG_NIBEGW_CONFIG_PATH);
  if (rename(tmp, CONFIG_NIBEGW_CONFIG_PATH) < 0)
    {
      return -errno;
    }

  return 0;
}
