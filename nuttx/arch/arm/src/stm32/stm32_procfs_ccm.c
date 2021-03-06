/****************************************************************************
 * arch/arm/src/stm32/stm32_procfs_ccm.c
 *
 *   Copyright (C) 2014 Pelle Windestam. All rights reserved.
 *   Author: Pelle Windestam (pelle@windestam.se)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/procfs.h>
#include <nuttx/fs/dirent.h>

#include <arch/irq.h>

#include "stm32_ccm.h"

#if !defined(CONFIG_DISABLE_MOUNTPOINT) && defined(CONFIG_FS_PROCFS)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define CCM_LINELEN     64

/****************************************************************************
 * Private Types
 ****************************************************************************/
/* This enumeration identifies all of the thread attributes that can be
 * accessed via the procfs file system.
 */

/* This structure describes one open "file" */

struct ccm_file_s
{
  struct procfs_file_s  base;    /* Base open file structure */
  unsigned int linesize;         /* Number of valid characters in line[] */
  char line[CCM_LINELEN];        /* Pre-allocated buffer for formatted lines */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
/* File system methods */

static int     ccm_open(FAR struct file *filep, FAR const char *relpath,
                        int oflags, mode_t mode);
static int     ccm_close(FAR struct file *filep);
static ssize_t ccm_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen);
static int     ccm_dup(FAR const struct file *oldp,
                       FAR struct file *newp);
static int     ccm_stat(FAR const char *relpath, FAR struct stat *buf);

/****************************************************************************
 * Private Variables
 ****************************************************************************/

/****************************************************************************
 * Public Variables
 ****************************************************************************/

/* See include/nutts/fs/procfs.h
 * We use the old-fashioned kind of initializers so that this will compile
 * with any compiler.
 */

const struct procfs_operations ccm_procfsoperations =
{
  ccm_open,       /* open */
  ccm_close,      /* close */
  ccm_read,       /* read */
  NULL,           /* write */
  ccm_dup,        /* dup */
  NULL,           /* opendir */
  NULL,           /* closedir */
  NULL,           /* readdir */
  NULL,           /* rewinddir */
  ccm_stat        /* stat */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ccm_open
 ****************************************************************************/

static int ccm_open(FAR struct file *filep, FAR const char *relpath,
                      int oflags, mode_t mode)
{
  FAR struct ccm_file_s *priv;

  fvdbg("Open '%s'\n", relpath);

  /* PROCFS is read-only.  Any attempt to open with any kind of write
   * access is not permitted.
   *
   * REVISIT:  Write-able proc files could be quite useful.
   */

  if ((oflags & O_WRONLY) != 0 || (oflags & O_RDONLY) == 0)
    {
      fdbg("ERROR: Only O_RDONLY supported\n");
      return -EACCES;
    }

  /* "cpuload" is the only acceptable value for the relpath */

  if (strcmp(relpath, "ccm") != 0)
    {
      fdbg("ERROR: relpath is '%s'\n", relpath);
      return -ENOENT;
    }

  /* Allocate a container to hold the task and attribute selection */

  priv = (FAR struct ccm_file_s *)kmm_zalloc(sizeof(struct ccm_file_s));
  if (!priv)
    {
      fdbg("ERROR: Failed to allocate file attributes\n");
      return -ENOMEM;
    }

  /* Save the index as the open-specific state in filep->f_priv */

  filep->f_priv = (FAR void *)priv;
  return OK;
}

/****************************************************************************
 * Name: ccm_close
 ****************************************************************************/

static int ccm_close(FAR struct file *filep)
{
  FAR struct ccm_file_s *priv;

  /* Recover our private data from the struct file instance */

  priv = (FAR struct ccm_file_s *)filep->f_priv;
  DEBUGASSERT(priv);

  /* Release the file attributes structure */

  kmm_free(priv);
  filep->f_priv = NULL;
  return OK;
}

/****************************************************************************
 * Name: ccm_read
 ****************************************************************************/

static ssize_t ccm_read(FAR struct file *filep, FAR char *buffer,
                           size_t buflen)
{
  FAR struct ccm_file_s *priv;
  ssize_t ret = 0;
  size_t linesize;
  size_t copysize;
  size_t remaining;
  size_t totalsize;
  struct mallinfo mem;
  off_t offset = filep->f_pos;

  fvdbg("buffer=%p buflen=%d\n", buffer, (int)buflen);

  /* Recover our private data from the struct file instance */

  priv = (FAR struct ccm_file_s *)filep->f_priv;
  DEBUGASSERT(priv);


  mm_mallinfo(&g_ccm_heap, &mem);
  
  remaining = buflen;
  totalsize = 0;

  linesize = snprintf(priv->line,
                      CCM_LINELEN,
                      "             total       used       free    largest\n");
  copysize = procfs_memcpy(priv->line, linesize, buffer, remaining, &offset);
  totalsize += copysize;
  buffer    += copysize;
  remaining -= copysize;

  if (totalsize >= buflen)
    {
      return totalsize;
    }

  linesize = snprintf(priv->line,
                      CCM_LINELEN,
                      "Mem:   %11d%11d%11d%11d\n",
                      mem.arena,
                      mem.uordblks,
                      mem.fordblks,
                      mem.mxordblk);
  copysize = procfs_memcpy(priv->line, linesize, buffer, remaining, &offset);
  totalsize += copysize;

  /* Update the file offset */

  if (totalsize > 0)
    {
      filep->f_pos += totalsize;
    }

  return totalsize;
}

/****************************************************************************
 * Name: ccm_dup
 *
 * Description:
 *   Duplicate open file data in the new file structure.
 *
 ****************************************************************************/

static int ccm_dup(FAR const struct file *oldp, FAR struct file *newp)
{
  FAR struct ccm_file_s *oldpriv;
  FAR struct ccm_file_s *newpriv;

  fvdbg("Dup %p->%p\n", oldp, newp);

  /* Recover our private data from the old struct file instance */

  oldpriv = (FAR struct ccm_file_s *)oldp->f_priv;
  DEBUGASSERT(oldpriv);

  /* Allocate a new container to hold the task and attribute selection */

  newpriv = (FAR struct ccm_file_s *)kmm_zalloc(sizeof(struct ccm_file_s));
  if (!newpriv)
    {
      fdbg("ERROR: Failed to allocate file attributes\n");
      return -ENOMEM;
    }

  /* The copy the file attributes from the old attributes to the new */

  memcpy(newpriv, oldpriv, sizeof(struct ccm_file_s));

  /* Save the new attributes in the new file structure */

  newp->f_priv = (FAR void *)newpriv;
  return OK;
}

static int ccm_stat(const char *relpath, struct stat *buf)
{
  if (strcmp(relpath, "ccm") != 0)
    {
      fdbg("ERROR: relpath is '%s'\n", relpath);
      return -ENOENT;
    }

  buf->st_mode    = S_IFREG|S_IROTH|S_IRGRP|S_IRUSR;
  buf->st_size    = 0;
  buf->st_blksize = 0;
  buf->st_blocks  = 0;

  return OK;
}

#endif /* !CONFIG_DISABLE_MOUNTPOINT && CONFIG_FS_PROCFS */
