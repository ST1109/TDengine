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

#include "trpc.h"
#include "query.h"
#include "tname.h"
#include "catalogInt.h"
#include "systable.h"

void ctgFreeMetaRent(SCtgRentMgmt *mgmt) {
  if (NULL == mgmt->slots) {
    return;
  }

  for (int32_t i = 0; i < mgmt->slotNum; ++i) {
    SCtgRentSlot *slot = &mgmt->slots[i];
    if (slot->meta) {
      taosArrayDestroy(slot->meta);
      slot->meta = NULL;
    }
  }

  taosMemoryFreeClear(mgmt->slots);
}


void ctgFreeTbMetaCache(SCtgTbMetaCache *cache) {
  CTG_LOCK(CTG_WRITE, &cache->stbLock);
  if (cache->stbCache) {
    int32_t stblNum = taosHashGetSize(cache->stbCache);  
    taosHashCleanup(cache->stbCache);
    cache->stbCache = NULL;
    CTG_CACHE_STAT_SUB(stblNum, stblNum);
  }
  CTG_UNLOCK(CTG_WRITE, &cache->stbLock);

  CTG_LOCK(CTG_WRITE, &cache->metaLock);
  if (cache->metaCache) {
    int32_t tblNum = taosHashGetSize(cache->metaCache);
    taosHashCleanup(cache->metaCache);
    cache->metaCache = NULL;
    CTG_CACHE_STAT_SUB(tblNum, tblNum);
  }
  CTG_UNLOCK(CTG_WRITE, &cache->metaLock);
}

void ctgFreeVgInfo(SDBVgInfo *vgInfo) {
  if (NULL == vgInfo) {
    return;
  }

  if (vgInfo->vgHash) {
    taosHashCleanup(vgInfo->vgHash);
    vgInfo->vgHash = NULL;
  }
  
  taosMemoryFreeClear(vgInfo);
}

void ctgFreeDbCache(SCtgDBCache *dbCache) {
  if (NULL == dbCache) {
    return;
  }

  CTG_LOCK(CTG_WRITE, &dbCache->vgLock);
  ctgFreeVgInfo (dbCache->vgInfo);
  CTG_UNLOCK(CTG_WRITE, &dbCache->vgLock);

  ctgFreeTbMetaCache(&dbCache->tbCache);
}


void ctgFreeHandle(SCatalog* pCtg) {
  ctgFreeMetaRent(&pCtg->dbRent);
  ctgFreeMetaRent(&pCtg->stbRent);
  
  if (pCtg->dbCache) {
    int32_t dbNum = taosHashGetSize(pCtg->dbCache);
    
    void *pIter = taosHashIterate(pCtg->dbCache, NULL);
    while (pIter) {
      SCtgDBCache *dbCache = pIter;

      atomic_store_8(&dbCache->deleted, 1);

      ctgFreeDbCache(dbCache);
            
      pIter = taosHashIterate(pCtg->dbCache, pIter);
    }  

    taosHashCleanup(pCtg->dbCache);
    
    CTG_CACHE_STAT_SUB(dbNum, dbNum);
  }
  
  taosMemoryFree(pCtg);
}

void ctgFreeMsgCtx(SCtgMsgCtx* pCtx) {
  if (pCtx->out) {
    free it;
  }

  taosMemoryFreeClear(pCtx->target);
}

void ctgFreeJob(void* job) {

}

int32_t ctgUpdateMsgCtx(SCtgMsgCtx* pCtx, int32_t reqType, void* out, char* target) {
  ctgFreeMsgCtx(pCtx);

  pCtx->reqType = reqType;
  pCtx->out = out;
  if (target) {
    pCtx->target = strdup(target);
    if (NULL == pCtx->target) {
      CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
    }
  } else {
    pCtx->target = NULL;
  }

  return TSDB_CODE_SUCCESS;
}


int32_t ctgGetHashFunction(int8_t hashMethod, tableNameHashFp *fp) {
  switch (hashMethod) {
    default:
      *fp = MurmurHash3_32;
      break;
  }

  return TSDB_CODE_SUCCESS;
}

int32_t ctgGenerateVgList(SCatalog *pCtg, SHashObj *vgHash, SArray** pList) {
  SHashObj *vgroupHash = NULL;
  SVgroupInfo *vgInfo = NULL;
  SArray *vgList = NULL;
  int32_t code = 0;
  int32_t vgNum = taosHashGetSize(vgHash);

  vgList = taosArrayInit(vgNum, sizeof(SVgroupInfo));
  if (NULL == vgList) {
    ctgError("taosArrayInit failed, num:%d", vgNum);
    CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);    
  }

  void *pIter = taosHashIterate(vgHash, NULL);
  while (pIter) {
    vgInfo = pIter;

    if (NULL == taosArrayPush(vgList, vgInfo)) {
      ctgError("taosArrayPush failed, vgId:%d", vgInfo->vgId);
      taosHashCancelIterate(vgHash, pIter);      
      CTG_ERR_JRET(TSDB_CODE_CTG_MEM_ERROR);
    }
    
    pIter = taosHashIterate(vgHash, pIter);
    vgInfo = NULL;
  }

  *pList = vgList;

  ctgDebug("Got vgList from cache, vgNum:%d", vgNum);

  return TSDB_CODE_SUCCESS;

_return:

  if (vgList) {
    taosArrayDestroy(vgList);
  }

  CTG_RET(code);
}


int32_t ctgGetVgInfoFromHashValue(SCatalog *pCtg, SDBVgInfo *dbInfo, const SName *pTableName, SVgroupInfo *pVgroup) {
  int32_t code = 0;
  
  int32_t vgNum = taosHashGetSize(dbInfo->vgHash);
  char db[TSDB_DB_FNAME_LEN] = {0};
  tNameGetFullDbName(pTableName, db);

  if (vgNum <= 0) {
    ctgError("db vgroup cache invalid, db:%s, vgroup number:%d", db, vgNum);
    CTG_ERR_RET(TSDB_CODE_TSC_DB_NOT_SELECTED);
  }

  tableNameHashFp fp = NULL;
  SVgroupInfo *vgInfo = NULL;

  CTG_ERR_RET(ctgGetHashFunction(dbInfo->hashMethod, &fp));

  char tbFullName[TSDB_TABLE_FNAME_LEN];
  tNameExtractFullName(pTableName, tbFullName);

  uint32_t hashValue = (*fp)(tbFullName, (uint32_t)strlen(tbFullName));

  void *pIter = taosHashIterate(dbInfo->vgHash, NULL);
  while (pIter) {
    vgInfo = pIter;
    if (hashValue >= vgInfo->hashBegin && hashValue <= vgInfo->hashEnd) {
      taosHashCancelIterate(dbInfo->vgHash, pIter);
      break;
    }
    
    pIter = taosHashIterate(dbInfo->vgHash, pIter);
    vgInfo = NULL;
  }

  if (NULL == vgInfo) {
    ctgError("no hash range found for hash value [%u], db:%s, numOfVgId:%d", hashValue, db, taosHashGetSize(dbInfo->vgHash));
    CTG_ERR_RET(TSDB_CODE_CTG_INTERNAL_ERROR);
  }

  *pVgroup = *vgInfo;

  CTG_RET(code);
}

int32_t ctgStbVersionSearchCompare(const void* key1, const void* key2) {
  if (*(uint64_t *)key1 < ((SSTableMetaVersion*)key2)->suid) {
    return -1;
  } else if (*(uint64_t *)key1 > ((SSTableMetaVersion*)key2)->suid) {
    return 1;
  } else {
    return 0;
  }
}

int32_t ctgDbVgVersionSearchCompare(const void* key1, const void* key2) {
  if (*(int64_t *)key1 < ((SDbVgVersion*)key2)->dbId) {
    return -1;
  } else if (*(int64_t *)key1 > ((SDbVgVersion*)key2)->dbId) {
    return 1;
  } else {
    return 0;
  }
}

int32_t ctgStbVersionSortCompare(const void* key1, const void* key2) {
  if (((SSTableMetaVersion*)key1)->suid < ((SSTableMetaVersion*)key2)->suid) {
    return -1;
  } else if (((SSTableMetaVersion*)key1)->suid > ((SSTableMetaVersion*)key2)->suid) {
    return 1;
  } else {
    return 0;
  }
}

int32_t ctgDbVgVersionSortCompare(const void* key1, const void* key2) {
  if (((SDbVgVersion*)key1)->dbId < ((SDbVgVersion*)key2)->dbId) {
    return -1;
  } else if (((SDbVgVersion*)key1)->dbId > ((SDbVgVersion*)key2)->dbId) {
    return 1;
  } else {
    return 0;
  }
}




int32_t ctgCloneVgInfo(SDBVgInfo *src, SDBVgInfo **dst) {
  *dst = taosMemoryMalloc(sizeof(SDBVgInfo));
  if (NULL == *dst) {
    qError("malloc %d failed", (int32_t)sizeof(SDBVgInfo));
    CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
  }

  memcpy(*dst, src, sizeof(SDBVgInfo));

  size_t hashSize = taosHashGetSize(src->vgHash);
  (*dst)->vgHash = taosHashInit(hashSize, taosGetDefaultHashFunction(TSDB_DATA_TYPE_INT), true, HASH_ENTRY_LOCK);
  if (NULL == (*dst)->vgHash) {
    qError("taosHashInit %d failed", (int32_t)hashSize);
    taosMemoryFreeClear(*dst);
    CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
  }

  int32_t *vgId = NULL;
  void *pIter = taosHashIterate(src->vgHash, NULL);
  while (pIter) {
    vgId = taosHashGetKey(pIter, NULL);

    if (taosHashPut((*dst)->vgHash, (void *)vgId, sizeof(int32_t), pIter, sizeof(SVgroupInfo))) {
      qError("taosHashPut failed, hashSize:%d", (int32_t)hashSize);
      taosHashCancelIterate(src->vgHash, pIter);
      taosHashCleanup((*dst)->vgHash);
      taosMemoryFreeClear(*dst);
      CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
    }
    
    pIter = taosHashIterate(src->vgHash, pIter);
  }


  return TSDB_CODE_SUCCESS;
}



int32_t ctgCloneMetaOutput(STableMetaOutput *output, STableMetaOutput **pOutput) {
  *pOutput = taosMemoryMalloc(sizeof(STableMetaOutput));
  if (NULL == *pOutput) {
    qError("malloc %d failed", (int32_t)sizeof(STableMetaOutput));
    CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
  }

  memcpy(*pOutput, output, sizeof(STableMetaOutput));

  if (output->tbMeta) {
    int32_t metaSize = CTG_META_SIZE(output->tbMeta);
    (*pOutput)->tbMeta = taosMemoryMalloc(metaSize);
    if (NULL == (*pOutput)->tbMeta) {
      qError("malloc %d failed", (int32_t)sizeof(STableMetaOutput));
      taosMemoryFreeClear(*pOutput);
      CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
    }

    memcpy((*pOutput)->tbMeta, output->tbMeta, metaSize);
  }

  return TSDB_CODE_SUCCESS;
}



