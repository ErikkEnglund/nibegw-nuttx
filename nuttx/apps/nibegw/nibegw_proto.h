/****************************************************************************
 * nibegw/nibegw_proto.h
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

#ifndef __NIBEGW_NIBEGW_PROTO_H
#define __NIBEGW_NIBEGW_PROTO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Frame format:
 *
 *   +----+------+------+-----+-----+----+----+-----+
 *   | 5C | ADDR | ADDR | CMD | LEN |  DATA   | CHK |
 *   +----+------+------+-----+-----+----+----+-----+
 *        |------------ CHK ------------------|
 *
 * Checksum is the XOR of ADDR..DATA.  When the checksum is 0x5C (the start
 * character) the heat pump sends 0xC5 instead.
 */

#define NIBEGW_START           0x5c
#define NIBEGW_ACK             0x06
#define NIBEGW_NAK             0x15

#define NIBEGW_CMD_READ_TOKEN  0x69
#define NIBEGW_CMD_WRITE_TOKEN 0x6b

#define NIBEGW_HDR_LEN         5     /* START, ADDR, ADDR, CMD, LEN */
#define NIBEGW_MAX_FRAME       (NIBEGW_HDR_LEN + 255 + 1)

/* Field accessors for a complete frame */

#define NIBEGW_FRAME_ADDR(f)   ((f)[2])
#define NIBEGW_FRAME_CMD(f)    ((f)[3])
#define NIBEGW_FRAME_LEN(f)    ((f)[4])

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum nibegw_push_e
{
  NIBEGW_PUSH_MORE = 0,  /* Byte consumed, frame not complete yet */
  NIBEGW_PUSH_FRAME,     /* Complete frame with valid checksum */
  NIBEGW_PUSH_BADCRC,    /* Complete frame with checksum mismatch */
  NIBEGW_PUSH_JUNK       /* Byte outside a frame, discarded */
};

struct nibegw_framer_s
{
  uint8_t buf[NIBEGW_MAX_FRAME];
  size_t  len;
  bool    started;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Return >0 frame length when data holds a complete, valid frame, 0 when
 * more bytes are needed, -1 when data does not start with NIBEGW_START and
 * -2 on checksum mismatch.
 */

int nibegw_check_frame(const uint8_t *data, size_t len);

void nibegw_framer_reset(struct nibegw_framer_s *f);

/* Feed one received byte.  On NIBEGW_PUSH_FRAME and NIBEGW_PUSH_BADCRC the
 * frame is in f->buf and its length in *framelen; it stays valid until the
 * next call.
 */

enum nibegw_push_e nibegw_framer_push(struct nibegw_framer_s *f,
                                      uint8_t byte, size_t *framelen);

#endif /* __NIBEGW_NIBEGW_PROTO_H */
