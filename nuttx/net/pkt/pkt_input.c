/****************************************************************************
 * net/pkt/pkt_input.c
 * Handling incoming packet input
 *
 *   Copyright (C) 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Adapted for NuttX from logic in uIP which also has a BSD-like license:
 *
 *   Original author Adam Dunkels <adam@dunkels.com>
 *   Copyright () 2001-2003, Adam Dunkels.
 *   All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#if defined(CONFIG_NET) && defined(CONFIG_NET_PKT)

#include <debug.h>

#include <nuttx/net/uip/uip.h>
#include <nuttx/net/uip/uip-arch.h>
#include <nuttx/net/uip/uip-pkt.h>
#include <nuttx/net/arp.h>

#include "uip/uip_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PKTBUF ((struct uip_eth_hdr *)&dev->d_buf)

/****************************************************************************
 * Public Variables
 ****************************************************************************/

/****************************************************************************
 * Private Variables
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: uip_pktinput
 *
 * Description:
 *   Handle incoming packet input
 *
 * Parameters:
 *   dev - The device driver structure containing the received packet
 *
 * Return:
 *   OK  The packet has been processed  and can be deleted
 *   ERROR Hold the packet and try again later. There is a listening socket
 *         but no recv in place to catch the packet yet.
 *
 * Assumptions:
 *   Called from the interrupt level or with interrupts disabled.
 *
 ****************************************************************************/

int uip_pktinput(struct uip_driver_s *dev)
{
  struct uip_pkt_conn *conn;
  struct uip_eth_hdr  *pbuf = (struct uip_eth_hdr *)dev->d_buf;
  int ret = OK;

  conn = uip_pktactive(pbuf);
  if (conn)
    {
      uint16_t flags;

      /* Setup for the application callback */

      dev->d_appdata = dev->d_buf;
      dev->d_snddata = dev->d_buf;
      dev->d_sndlen  = 0;

      /* Perform the application callback */

      flags = uip_pktcallback(dev, conn, UIP_NEWDATA);

      /* If the operation was successful, the UIP_NEWDATA flag is removed
       * and thus the packet can be deleted (OK will be returned).
       */

      if ((flags & UIP_NEWDATA) != 0)
        {
          /* No.. the packet was not processed now.  Return ERROR so
           * that the driver may retry again later.
           */

          ret = ERROR;
        }
    }
  else
    {
      nlldbg("No listener\n");
    }

  return ret;
}

#endif /* CONFIG_NET && CONFIG_NET_PKT */