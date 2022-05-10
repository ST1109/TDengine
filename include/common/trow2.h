/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _TD_TROW_H_
#define _TD_TROW_H_

#include "tdataformat.h"
#include "tdef.h"
#include "tencode.h"
#include "tmsg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct STSRow2       STSRow2;
typedef struct STSRowBuilder STSRowBuilder;

// STSRow2
int32_t tEncodeSTSRow(SEncoder *pEncoder, const STSRow2 *pRow);
int32_t tDecodeSTSRow(SDecoder *pDecoder, STSRow2 *pRow);

// STSRowBuilder
int32_t tRowBuilderInit(STSRowBuilder *pRB, const SSchema *pSchema, int nCols);
int32_t tRowBuilderClear(STSRowBuilder *pRB);
int32_t tRowBuilderReset(STSRowBuilder *pRB);
int32_t tRowBuilderPut(STSRowBuilder *pRB, int16_t cid, const uint8_t *pData, int32_t nData, int32_t flags);
int32_t tRowBuilderGet(STSRowBuilder *pRB, const STSRow2 *pRow);

// STRUCTS
#define TD_KV_ROW 0x1
struct STSRow2 {
  TSKEY    ts;
  uint32_t flags;
  union {
    int32_t sver;
    int32_t ncols;
  };
  uint32_t       nData;
  const uint8_t *pData;
};

struct STSRowBuilder {
  STSchema *pSchema;
  uint8_t  *pBuf;
  STSRow2   row;
};

#ifdef __cplusplus
}
#endif

#endif /*_TD_TROW_H_*/