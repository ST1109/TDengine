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

#include "trow2.h"

inline int32_t tEncodeSTSRow(SEncoder *pEncoder, const STSRow2 *pRow) {
  if (tEncodeI64(pEncoder, pRow->ts) < 0) return -1;
  if (tEncodeU32v(pEncoder, pRow->flags) < 0) return -1;
  if (pRow->flags & TD_KV_ROW) {
    if (tEncodeI32v(pEncoder, pRow->ncols) < 0) return -1;
  } else {
    if (tEncodeI32v(pEncoder, pRow->sver) < 0) return -1;
  }
  if (tEncodeBinary(pEncoder, pRow->pData, pRow->nData) < 0) return -1;
  return 0;
}

inline int32_t tDecodeSTSRow(SDecoder *pDecoder, STSRow2 *pRow) {
  if (tDecodeI64(pDecoder, &pRow->ts) < 0) return -1;
  if (tDecodeU32v(pDecoder, &pRow->flags) < 0) return -1;
  if (pRow->flags & TD_KV_ROW) {
    if (tDecodeI32v(pDecoder, &pRow->ncols) < 0) return -1;
  } else {
    if (tDecodeI32v(pDecoder, &pRow->sver) < 0) return -1;
  }
  if (tDecodeBinary(pDecoder, &pRow->pData, &pRow->nData) < 0) return -1;
  return 0;
}

int32_t tRowBuilderInit(STSRowBuilder *pRB, const SSchema *pSchema, int nCols) {
  // TODO
  return 0;
}

int32_t tRowBuilderClear(STSRowBuilder *pRB) {
  // TODO
  return 0;
}

int32_t tRowBuilderReset(STSRowBuilder *pRB) {
  // TODO
  return 0;
}

int32_t tRowBuilderPut(STSRowBuilder *pRB, int16_t cid, const uint8_t *pData, int32_t nData, int32_t flags) {
  // TODO
  return 0;
}

int32_t tRowBuilderGet(STSRowBuilder *pRB, const STSRow2 *pRow) {
  // TODO
  return 0;
}