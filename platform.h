/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erikn@it.uu.se>
 */
#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#ifdef __KERNEL__
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#else
#include <stdlib.h>
#include <errno.h>
#include "list.h"

#define kmalloc(sz, alloc) malloc(sz)
#define kfree(ptr) free(ptr)

#endif /* __KERNEL__ */

#include "lock.h"

#endif /* __PLATFORM_H__ */
