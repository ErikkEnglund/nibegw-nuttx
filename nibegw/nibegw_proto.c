/****************************************************************************
 * nibegw/nibegw_proto.c
 *
 * Copyright (c) 2010-2026 Contributors to the openHAB project
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * Frame parsing from nibegw.c by pauli.anttila@gmail.com.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "nibegw_proto.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int nibegw_check_frame(const uint8_t *data, size_t len)
{
  uint8_t calc;
  uint8_t recv;
  size_t datalen;
  size_t i;

  if (len < 1)
    {
      return 0;
    }

  if (data[0] != NIBEGW_START)
    {
      return -1;
    }

  if (len < NIBEGW_HDR_LEN + 1)
    {
      return 0;
    }

  datalen = data[4];
  if (len < datalen + NIBEGW_HDR_LEN + 1)
    {
      return 0;
    }

  calc = 0;
  for (i = 1; i < datalen + NIBEGW_HDR_LEN; i++)
    {
      calc ^= data[i];
    }

  recv = data[datalen + NIBEGW_HDR_LEN];

  /* The heat pump sends 0xC5 when the checksum would be the start
   * character 0x5C.
   */

  if (calc != recv && !(calc == NIBEGW_START && recv == 0xc5))
    {
      return -2;
    }

  return (int)(datalen + NIBEGW_HDR_LEN + 1);
}

void nibegw_framer_reset(struct nibegw_framer_s *f)
{
  f->len = 0;
  f->started = false;
}

enum nibegw_push_e nibegw_framer_push(struct nibegw_framer_s *f,
                                      uint8_t byte, size_t *framelen)
{
  int ret;

  if (!f->started)
    {
      if (byte != NIBEGW_START)
        {
          return NIBEGW_PUSH_JUNK;
        }

      f->started = true;
      f->len = 0;
    }

  /* NIBEGW_MAX_FRAME covers the largest possible LEN, so a started frame
   * always completes before the buffer is full.
   */

  f->buf[f->len++] = byte;

  ret = nibegw_check_frame(f->buf, f->len);
  if (ret == 0)
    {
      return NIBEGW_PUSH_MORE;
    }

  f->started = false;
  *framelen = f->len;

  return ret > 0 ? NIBEGW_PUSH_FRAME : NIBEGW_PUSH_BADCRC;
}
