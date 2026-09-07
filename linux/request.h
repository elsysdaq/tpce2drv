/******************************************************************************
 *                                                                             *
 * License Agreement                                                           *
 *                                                                             *
 * Copyright (c) 2026 Elsys AG, Niederrohrdorf, Switzerland                    *
 * All rights reserved.                                                        *
 *                                                                             *
 * Permission is hereby granted, free of charge, to any person obtaining a     *
 * copy of this software and associated documentation files (the "Software"),  *
 * to deal in the Software without restriction, including without limitation   *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,    *
 * and/or sell copies of the Software, and to permit persons to whom the       *
 * Software is furnished to do so, subject to the following conditions:        *
 *                                                                             *
 * The above copyright notice and this permission notice shall be included in  *
 * all copies or substantial portions of the Software.                         *
 *                                                                             *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,    *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE *
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      *
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING     *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER         *
 * DEALINGS IN THE SOFTWARE.                                                   *
 *                                                                             *
 * This agreement shall be governed in all respects by the laws of             *
 * Switzerland.                                                                *
 *                                                                             *
 ******************************************************************************/
#ifndef TPCE_REQUEST_H
#define TPCE_REQUEST_H

#include "tpce.h"

int tpce_init_stream_info(internal_dma_stream_info *info);
void tpce_free_stream_info(internal_dma_stream_info *info);
void tpce_safe_dma_unmap(tpce_dev *tpce, tpce_dma_map_info *map_info);
void reset_dma_info(tpce_dev *tpce, tpce_dma_info *info);

int tpce_dispatch_ioc(tpce_dev *tpce, unsigned int ioc, internal_ioctl_data *ioctl_data);

#endif /* TPCE_REQUEST_H */
