/**
 * @file
 * log abstraction layer
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Yonggang Luo <luoyonggang@gmail.com>
 */

#ifndef LWIP_HDR_LOG_H
#define LWIP_HDR_LOG_H

#include "arch.h"

#ifndef LWIP_LOCAL_LOG_LEVEL
#define LWIP_LOCAL_LOG_LEVEL LWIP_LOG_LEVEL_INFO
#endif

#ifndef LWIP_LOCAL_LOG_TAG
#ifdef LWIP_LOG_TAG
#define LWIP_LOCAL_LOG_TAG LWIP_LOG_TAG
#else
#define LWIP_LOCAL_LOG_TAG LWIP_LOG_TAG_NONE
#endif /* LWIP_LOG_TAG */
#endif /* LWIP_LOCAL_LOG_TAG */

#define LWIP_LOGE_EN (LWIP_LOCAL_LOG_LEVEL >= LWIP_LOG_LEVEL_ERROR)
#define LWIP_LOGW_EN (LWIP_LOCAL_LOG_LEVEL >= LWIP_LOG_LEVEL_WARN)
#define LWIP_LOGI_EN (LWIP_LOCAL_LOG_LEVEL >= LWIP_LOG_LEVEL_INFO)
#define LWIP_LOGD_EN (LWIP_LOCAL_LOG_LEVEL >= LWIP_LOG_LEVEL_DEBUG)
#define LWIP_LOGV_EN (LWIP_LOCAL_LOG_LEVEL >= LWIP_LOG_LEVEL_VERBOSE)

static void lwip_vprintf_level(unsigned level, const char *fmt, va_list ap)
{
  if (LWIP_LOCAL_LOG_LEVEL >= level) {
    lwip_vprintf((level << 28) | (LWIP_LOCAL_LOG_TAG), fmt, ap);
  }
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static void lwip_printf_level(unsigned level, const char *fmt, ...)
{
  if (LWIP_LOCAL_LOG_LEVEL >= level) {
    va_list ap;
    va_start(ap, fmt);
    lwip_vprintf((level << 28) | (LWIP_LOCAL_LOG_TAG), fmt, ap);
    va_end(ap);
  }
}

static void lwip_printfe(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  lwip_vprintf_level(LWIP_LOG_LEVEL_ERROR, fmt, ap);
  va_end(ap);
}

static void lwip_printfw(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  lwip_vprintf_level(LWIP_LOG_LEVEL_WARN, fmt, ap);
  va_end(ap);
}

static void lwip_printfi(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  lwip_vprintf_level(LWIP_LOG_LEVEL_INFO, fmt, ap);
  va_end(ap);
}

static void lwip_printfd(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  lwip_vprintf_level(LWIP_LOG_LEVEL_DEBUG, fmt, ap);
  va_end(ap);
}

static void lwip_printfv(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  lwip_vprintf_level(LWIP_LOG_LEVEL_VERBOSE, fmt, ap);
  va_end(ap);
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif /* LWIP_HDR_LOG_H */
