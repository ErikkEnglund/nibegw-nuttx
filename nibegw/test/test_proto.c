/****************************************************************************
 * nibegw/test/test_proto.c
 *
 * Host unit tests for the frame parser.  Build and run with "make" in this
 * directory.
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "nibegw_proto.h"

static int g_failures;

#define CHECK(cond) \
  do \
    { \
      if (!(cond)) \
        { \
          printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
          g_failures++; \
        } \
    } \
  while (0)

/* Frames captured from a NIBE F730 on 2026-10-02 */

static const uint8_t g_read_token[]  = { 0x5c, 0x00, 0x20, 0x69, 0x00, 0x49 };
static const uint8_t g_write_token[] = { 0x5c, 0x00, 0x20, 0x6b, 0x00, 0x4b };
static const uint8_t g_ee[]          = { 0x5c, 0x00, 0x20, 0xee, 0x00, 0xce };
static const uint8_t g_write_resp[]  = { 0x5c, 0x00, 0x20, 0x6c, 0x01, 0x01,
                                         0x4c };
static const uint8_t g_read_resp[]   = { 0x5c, 0x00, 0x20, 0x6a, 0x06, 0xc9,
                                         0xb7, 0xd6, 0x01, 0x01, 0x00, 0xe4 };
static const uint8_t g_product[]     = { 0x5c, 0x00, 0x20, 0x6d, 0x11, 0x01,
                                         0x23, 0x81, 0x46, 0x37, 0x33, 0x30,
                                         0x20, 0x43, 0x55, 0x20, 0x33, 0x78,
                                         0x34, 0x30, 0x30, 0x56, 0xb2 };

/* Data frame from the test data in the original nibegw.c */

static const uint8_t g_data[] =
{
  0x5c, 0x00, 0x20, 0x68, 0x50, 0x01, 0xa8, 0x1f, 0x01, 0x00, 0xa8, 0x64,
  0x00, 0xfd, 0xa7, 0xd0, 0x03, 0x44, 0x9c, 0x1e, 0x00, 0x4f, 0x9c, 0xa0,
  0x00, 0x50, 0x9c, 0x78, 0x00, 0x51, 0x9c, 0x03, 0x01, 0x52, 0x9c, 0x1b,
  0x01, 0x87, 0x9c, 0x14, 0x01, 0x4e, 0x9c, 0xc6, 0x01, 0x47, 0x9c, 0x01,
  0x01, 0x15, 0xb9, 0xb0, 0xff, 0x3a, 0xb9, 0x4b, 0x00, 0xc9, 0xaf, 0x00,
  0x00, 0x48, 0x9c, 0x0d, 0x01, 0x4c, 0x9c, 0xe7, 0x00, 0x4b, 0x9c, 0x00,
  0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00,
  0x00, 0x45
};

/* Feed a buffer and count the results */

struct counts_s
{
  int frames;
  int badcrc;
  int junk;
  size_t lastlen;
};

static void feed(struct nibegw_framer_s *f, const uint8_t *data, size_t len,
                 struct counts_s *c)
{
  size_t framelen;
  size_t i;

  for (i = 0; i < len; i++)
    {
      switch (nibegw_framer_push(f, data[i], &framelen))
        {
          case NIBEGW_PUSH_FRAME:
            c->frames++;
            c->lastlen = framelen;
            break;

          case NIBEGW_PUSH_BADCRC:
            c->badcrc++;
            c->lastlen = framelen;
            break;

          case NIBEGW_PUSH_JUNK:
            c->junk++;
            break;

          default:
            break;
        }
    }
}

static void test_check_frame_real(void)
{
  CHECK(nibegw_check_frame(g_read_token, sizeof(g_read_token)) == 6);
  CHECK(nibegw_check_frame(g_write_token, sizeof(g_write_token)) == 6);
  CHECK(nibegw_check_frame(g_ee, sizeof(g_ee)) == 6);
  CHECK(nibegw_check_frame(g_write_resp, sizeof(g_write_resp)) == 7);
  CHECK(nibegw_check_frame(g_read_resp, sizeof(g_read_resp)) == 12);
  CHECK(nibegw_check_frame(g_product, sizeof(g_product)) == 23);
  CHECK(nibegw_check_frame(g_data, sizeof(g_data)) == 86);
}

static void test_check_frame_partial(void)
{
  size_t i;

  CHECK(nibegw_check_frame(g_data, 0) == 0);

  for (i = 1; i < sizeof(g_data); i++)
    {
      CHECK(nibegw_check_frame(g_data, i) == 0);
    }
}

static void test_check_frame_bad_start(void)
{
  const uint8_t frame[] = { 0x5d, 0x00, 0x20, 0x69, 0x00, 0x49 };

  CHECK(nibegw_check_frame(frame, sizeof(frame)) == -1);
}

static void test_check_frame_bad_checksum(void)
{
  uint8_t frame[sizeof(g_product)];

  memcpy(frame, g_product, sizeof(frame));
  frame[10] ^= 0x01;
  CHECK(nibegw_check_frame(frame, sizeof(frame)) == -2);
}

static void test_checksum_5c_special_case(void)
{
  /* 0x00 ^ 0x20 ^ 0x7c ^ 0x00 == 0x5c */

  const uint8_t as_c5[] = { 0x5c, 0x00, 0x20, 0x7c, 0x00, 0xc5 };
  const uint8_t as_5c[] = { 0x5c, 0x00, 0x20, 0x7c, 0x00, 0x5c };
  const uint8_t wrong[] = { 0x5c, 0x00, 0x20, 0x7c, 0x00, 0xc4 };

  CHECK(nibegw_check_frame(as_c5, sizeof(as_c5)) == 6);
  CHECK(nibegw_check_frame(as_5c, sizeof(as_5c)) == 6);
  CHECK(nibegw_check_frame(wrong, sizeof(wrong)) == -2);
}

static void test_c5_only_valid_for_5c(void)
{
  /* The original code accepted any frame whose checksum byte was 0xC5.
   * The real checksum here is 0x4B.
   */

  const uint8_t frame[] = { 0x5c, 0x00, 0x20, 0x6b, 0x00, 0xc5 };

  CHECK(nibegw_check_frame(frame, sizeof(frame)) == -2);
}

static void test_framer_stream(void)
{
  struct nibegw_framer_s f;
  struct counts_s c;
  uint8_t stream[256];
  size_t n = 0;

  /* junk, token, data frame, junk, product frame back to back */

  stream[n++] = 0x01;
  stream[n++] = 0x02;
  memcpy(&stream[n], g_read_token, sizeof(g_read_token));
  n += sizeof(g_read_token);
  memcpy(&stream[n], g_data, sizeof(g_data));
  n += sizeof(g_data);
  stream[n++] = 0x06;
  memcpy(&stream[n], g_product, sizeof(g_product));
  n += sizeof(g_product);

  memset(&c, 0, sizeof(c));
  nibegw_framer_reset(&f);
  feed(&f, stream, n, &c);

  CHECK(c.frames == 3);
  CHECK(c.badcrc == 0);
  CHECK(c.junk == 3);
  CHECK(c.lastlen == sizeof(g_product));
  CHECK(memcmp(f.buf, g_product, sizeof(g_product)) == 0);
}

static void test_framer_badcrc_then_recovers(void)
{
  struct nibegw_framer_s f;
  struct counts_s c;
  uint8_t bad[sizeof(g_write_token)];

  memcpy(bad, g_write_token, sizeof(bad));
  bad[5] = 0x00;

  memset(&c, 0, sizeof(c));
  nibegw_framer_reset(&f);
  feed(&f, bad, sizeof(bad), &c);

  CHECK(c.badcrc == 1);
  CHECK(c.lastlen == sizeof(bad));
  CHECK(NIBEGW_FRAME_ADDR(f.buf) == 0x20);

  feed(&f, g_write_token, sizeof(g_write_token), &c);
  CHECK(c.frames == 1);
  CHECK(NIBEGW_FRAME_CMD(f.buf) == NIBEGW_CMD_WRITE_TOKEN);
}

static void test_framer_max_len(void)
{
  struct nibegw_framer_s f;
  struct counts_s c;
  uint8_t frame[NIBEGW_MAX_FRAME];
  uint8_t chk = 0;
  size_t i;

  frame[0] = NIBEGW_START;
  frame[1] = 0x00;
  frame[2] = 0x20;
  frame[3] = 0x68;
  frame[4] = 0xff;
  for (i = 5; i < NIBEGW_MAX_FRAME - 1; i++)
    {
      frame[i] = (uint8_t)i;
    }

  for (i = 1; i < NIBEGW_MAX_FRAME - 1; i++)
    {
      chk ^= frame[i];
    }

  frame[NIBEGW_MAX_FRAME - 1] = chk == NIBEGW_START ? 0xc5 : chk;

  memset(&c, 0, sizeof(c));
  nibegw_framer_reset(&f);
  feed(&f, frame, sizeof(frame), &c);

  CHECK(c.frames == 1);
  CHECK(c.lastlen == NIBEGW_MAX_FRAME);
}

int main(void)
{
  test_check_frame_real();
  test_check_frame_partial();
  test_check_frame_bad_start();
  test_check_frame_bad_checksum();
  test_checksum_5c_special_case();
  test_c5_only_valid_for_5c();
  test_framer_stream();
  test_framer_badcrc_then_recovers();
  test_framer_max_len();

  if (g_failures != 0)
    {
      printf("%d check(s) failed\n", g_failures);
      return 1;
    }

  printf("all tests passed\n");
  return 0;
}
