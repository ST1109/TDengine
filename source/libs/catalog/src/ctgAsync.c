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
#include "tref.h"

int32_t ctgInitGetTbMetaTask(SCtgJob *pJob, int32_t taskIdx, SName *name) {
  SCtgTask *pTask = taosArrayGet(pJob->pTasks, taskIdx);

  pTask->type = CTG_TASK_GET_TB_META;
  pTask->taskId = taskIdx;
  pTask->pJob = pJob;

  pTask->taskCtx = taosMemoryCalloc(1, sizeof(SCtgTbMetaCtx));
  if (NULL == pTask->taskCtx) {
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }

  SCtgTbMetaCtx* ctx = pTask->taskCtx;
  ctx->pName = taosMemoryMalloc(sizeof(*name));
  if (NULL == ctx->pName) {
    taosMemoryFree(pTask->taskCtx);
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }
  
  memcpy(ctx->pName, name, sizeof(*name));
  ctx->flag = CTG_FLAG_UNKNOWN_STB;

  return TSDB_CODE_SUCCESS;
}

int32_t ctgInitGetDbVgTask(SCtgJob *pJob, int32_t taskIdx, char *dbFName) {
  SCtgTask *pTask = taosArrayGet(pJob->pTasks, taskIdx);

  pTask->type = CTG_TASK_GET_DB_VGROUP;
  pTask->taskId = taskIdx;
  pTask->pJob = pJob;

  pTask->taskCtx = taosMemoryCalloc(1, sizeof(SCtgDbVgCtx));
  if (NULL == pTask->taskCtx) {
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }

  SCtgDbVgCtx* ctx = pTask->taskCtx;
  
  memcpy(ctx->dbFName, dbFName, sizeof(ctx->dbFName));

  return TSDB_CODE_SUCCESS;
}

int32_t ctgInitGetTbHashTask(SCtgJob *pJob, int32_t taskIdx, SName *name) {
  SCtgTask *pTask = taosArrayGet(pJob->pTasks, taskIdx);

  pTask->type = CTG_TASK_GET_TB_HASH;
  pTask->taskId = taskIdx;
  pTask->pJob = pJob;

  pTask->taskCtx = taosMemoryCalloc(1, sizeof(SCtgTbHashCtx));
  if (NULL == pTask->taskCtx) {
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }

  SCtgTbHashCtx* ctx = pTask->taskCtx;
  ctx->pName = taosMemoryMalloc(sizeof(*name));
  if (NULL == ctx->pName) {
    taosMemoryFree(pTask->taskCtx);
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }
  
  memcpy(ctx->pName, name, sizeof(*name));
  tNameGetFullDbName(ctx->pName, ctx->dbFName);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgInitJob(CTG_PARAMS, SCtgJob** job, uint64_t reqId, const SCatalogReq* pReq, catalogCallback fp, void* param) {
  int32_t code = 0;
  int32_t tbMetaNum = (int32_t)taosArrayGetSize(pReq->pTableMeta);
  int32_t dbNum = (int32_t)taosArrayGetSize(pReq->pDb);
  int32_t udfNum = (int32_t)taosArrayGetSize(pReq->pUdf);
  int32_t tbHashNum = (int32_t)taosArrayGetSize(pReq->pTableHash);

  int32_t taskNum = tbMetaNum + dbNum + udfNum + tbHashNum;
  if (taskNum <= 0) {
    ctgError("empty input for job, taskNum:%d", taskNum);
    CTG_ERR_RET(TSDB_CODE_CTG_INVALID_INPUT);
  }
  
  *job = taosMemoryCalloc(1, sizeof(SCtgJob));
  if (NULL == *job) {
    ctgError("calloc %d failed", (int32_t)sizeof(SCtgJob));
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }

  SCtgJob *pJob = *job;
  
  pJob->queryId = reqId;
  pJob->userFp = fp;
  pJob->userParam = param;
  pJob->tbMetaNum = tbMetaNum;
  pJob->tbHashNum = tbHashNum;
  pJob->dbNum = dbNum;
  pJob->udfNum = udfNum;
  
  pJob->pCtg    = pCtg;
  pJob->pTrans    = pTrans;
  pJob->pMgmtEps    = pMgmtEps;
  pJob->pTasks = taosArrayInit(taskNum, sizeof(SCtgTask));
  
  if (NULL == pJob->pTasks) {
    ctgError("taosArrayInit %d tasks failed", taskNum);
    CTG_ERR_JRET(TSDB_CODE_OUT_OF_MEMORY);
  }

  int32_t taskIdx = 0;
  for (int32_t i = 0; i < tbMetaNum; ++i) {
    SName *name = taosArrayGet(pReq->pTableMeta, i);
    CTG_ERR_JRET(ctgInitGetTbMetaTask(pJob, taskIdx++, name));
  }

  for (int32_t i = 0; i < dbNum; ++i) {
    char *dbFName = taosArrayGet(pReq->pDb, i);
    CTG_ERR_JRET(ctgInitGetDbVgTask(pJob, taskIdx++, dbFName));
  }

  for (int32_t i = 0; i < tbHashNum; ++i) {
    SName *name = taosArrayGet(pReq->pTableHash, i);
    CTG_ERR_JRET(ctgInitGetTbHashTask(pJob, taskIdx++, name));
  }


  pJob->refId = taosAddRef(gCtgMgmt.jobPool, pJob);
  if (pJob->refId < 0) {
    ctgError("add job to ref failed, error: %s", tstrerror(terrno));
    CTG_ERR_JRET(terrno);
  }

  taosAcquireRef(gCtgMgmt.jobPool, pJob->refId);

  return TSDB_CODE_SUCCESS;

_return:

  taosMemoryFreeClear(*job);

  CTG_RET(code);
}

int32_t ctgDumpTbMetaRes(SCtgTask* pTask) {
  SCtgJob* pJob = pTask->pJob;
  if (NULL == pJob->jobRes.pTableMeta) {
    pJob->jobRes.pTableMeta = taosArrayInit(pJob->tbMetaNum, sizeof(STableMeta));
    if (NULL == pJob->jobRes.pTableMeta) {
      CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
    }
  }

  taosArrayPush(pJob->jobRes.pTableMeta, pTask->res);

  taosMemoryFreeClear(pTask->res);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgDumpDbVgRes(SCtgTask* pTask) {
  SCtgJob* pJob = pTask->pJob;
  if (NULL == pJob->jobRes.pDbVgroup) {
    pJob->jobRes.pDbVgroup = taosArrayInit(pJob->dbNum, POINTER_BYTES);
    if (NULL == pJob->jobRes.pDbVgroup) {
      CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
    }
  }

  taosArrayPush(pJob->jobRes.pDbVgroup, &pTask->res);
  pTask->res = NULL;

  return TSDB_CODE_SUCCESS;
}

int32_t ctgDumpTbHashRes(SCtgTask* pTask) {
  SCtgJob* pJob = pTask->pJob;
  if (NULL == pJob->jobRes.pTableHash) {
    pJob->jobRes.pTableHash = taosArrayInit(pJob->tbHashNum, sizeof(SVgroupInfo));
    if (NULL == pJob->jobRes.pTableHash) {
      CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
    }
  }

  taosArrayPush(pJob->jobRes.pTableHash, &pTask->res);

  return TSDB_CODE_SUCCESS;
}


int32_t ctgDumpUdfRes(SCtgTask* pTask) {
  SCtgJob* pJob = pTask->pJob;
  if (NULL == pJob->jobRes.pUdfList) {
    pJob->jobRes.pUdfList = taosArrayInit(pJob->udfNum, sizeof(SFuncInfo));
    if (NULL == pJob->jobRes.pUdfList) {
      CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
    }
  }

  taosArrayPush(pJob->jobRes.pUdfList, pTask->res);

  taosMemoryFreeClear(pTask->res);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgHandleTaskEnd(SCtgTask* pTask, int32_t rspCode) {
  SCtgJob* pJob = pTask->pJob;
  int32_t code = 0;

  if (rspCode) {
    int32_t lastCode = atomic_val_compare_exchange_32(&pJob->rspCode, 0, rspCode);
    if (0 == lastCode) {
      (*pJob->userFp)(NULL, pJob->userParam, rspCode);
    }

    return TSDB_CODE_SUCCESS;  
  }

  int32_t taskDone = atomic_add_fetch_32(&pJob->taskDone, 1);
  if (taskDone < taosArrayGetSize(pJob->pTasks)) {
    qDebug("task done: %d, total: %d", taskDone, (int32_t)taosArrayGetSize(pJob->pTasks));
    return TSDB_CODE_SUCCESS;
  }

  CTG_ERR_JRET(ctgMakeAsyncRes(pJob));

  (*pJob->userFp)(&pJob->jobRes, pJob->userParam, 0);

  return TSDB_CODE_SUCCESS;

_return:

  (*pJob->userFp)(NULL, pJob->userParam, code);
  CTG_RET(code);
}

int32_t ctgHandleGetTbMetaRsp(SCtgTask* pTask, int32_t reqType, const SDataBuf *pMsg, int32_t rspCode) {
  int32_t code = 0;
  SCtgDBCache *dbCache = NULL;
  CTG_ERR_JRET(ctgProcessRspMsg(pTask->msgCtx.out, reqType, pMsg->pData, pMsg->len, rspCode, pTask->msgCtx.target));

  SCtgTbMetaCtx* ctx = (SCtgTbMetaCtx*)pTask->taskCtx;
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;

  switch (reqType) {
    case TDMT_MND_USE_DB: {
      SUseDbOutput* pOut = (SUseDbOutput*)pTask->msgCtx.out;

      SVgroupInfo vgInfo = {0};
      CTG_ERR_JRET(ctgGetVgInfoFromHashValue(pCtg, pOut->dbVgroup, ctx->pName, &vgInfo));

      ctgDebug("will refresh tbmeta, not supposed to be stb, tbName:%s, flag:%d", tNameGetTableName(ctx->pName), ctx->flag);

      CTG_ERR_JRET(ctgGetTbMetaFromVnode(CTG_PARAMS_LIST(), ctx->pName, &vgInfo, NULL, pTask));

      return TSDB_CODE_SUCCESS;
    }
    case TDMT_MND_TABLE_META: {
      STableMetaOutput* pOut = (STableMetaOutput*)pTask->msgCtx.out;
      
      if (CTG_IS_META_NULL(pOut->metaType)) {
        if (CTG_FLAG_IS_STB(ctx->flag)) {
          char dbFName[TSDB_DB_FNAME_LEN] = {0};
          tNameGetFullDbName(ctx->pName, dbFName);
          
          CTG_ERR_RET(ctgAcquireVgInfoFromCache(pCtg, dbFName, &dbCache));
          if (NULL != dbCache) {
            SVgroupInfo vgInfo = {0};
            CTG_ERR_JRET(ctgGetVgInfoFromHashValue(pCtg, dbCache->vgInfo, ctx->pName, &vgInfo));
          
            ctgDebug("will refresh tbmeta, not supposed to be stb, tbName:%s, flag:%d", tNameGetTableName(ctx->pName), ctx->flag);
          
            CTG_ERR_JRET(ctgGetTbMetaFromVnode(CTG_PARAMS_LIST(), ctx->pName, &vgInfo, NULL, pTask));

            ctgReleaseVgInfo(dbCache);
            ctgReleaseDBCache(pCtg, dbCache);
          } else {
            SBuildUseDBInput input = {0};
          
            tstrncpy(input.db, dbFName, tListLen(input.db));
            input.vgVersion = CTG_DEFAULT_INVALID_VERSION;
          
            CTG_ERR_JRET(ctgGetDBVgInfoFromMnode(pCtg, pTrans, pMgmtEps, &input, NULL, pTask));
          }

          return TSDB_CODE_SUCCESS;
        }
        
        ctgError("no tbmeta got, tbNmae:%s", tNameGetTableName(ctx->pName));
        ctgRemoveTbMetaFromCache(pCtg, ctx->pName, false);
        
        CTG_ERR_JRET(CTG_ERR_CODE_TABLE_NOT_EXIST);
      }

      if (pTask->msgCtx.lastOut) {
        TSWAP(pTask->msgCtx.out, pTask->msgCtx.lastOut);
        STableMetaOutput* pLastOut = (STableMetaOutput*)pTask->msgCtx.out;
        TSWAP(pLastOut->tbMeta, pOut->tbMeta);
      }
      
      break;
    }
    case TDMT_VND_TABLE_META: {
      STableMetaOutput* pOut = (STableMetaOutput*)pTask->msgCtx.out;
      
      if (CTG_IS_META_NULL(pOut->metaType)) {
        ctgError("no tbmeta got, tbNmae:%s", tNameGetTableName(ctx->pName));
        ctgRemoveTbMetaFromCache(pCtg, ctx->pName, false);
        CTG_ERR_JRET(CTG_ERR_CODE_TABLE_NOT_EXIST);
      }

      if (CTG_FLAG_IS_STB(ctx->flag)) {
        break;
      }
      
      if (CTG_IS_META_TABLE(pOut->metaType) && TSDB_SUPER_TABLE == pOut->tbMeta->tableType) {
        ctgDebug("will continue to refresh tbmeta since got stb, tbName:%s", tNameGetTableName(ctx->pName));
      
        taosMemoryFreeClear(pOut->tbMeta);
        
        CTG_ERR_JRET(ctgGetTbMetaFromMnode(CTG_PARAMS_LIST(), ctx->pName, NULL, pTask));
      } else if (CTG_IS_META_BOTH(pOut->metaType)) {
        int32_t exist = 0;
        if (!CTG_FLAG_IS_FORCE_UPDATE(ctx->flag)) {
          CTG_ERR_JRET(ctgTbMetaExistInCache(pCtg, pOut->dbFName, pOut->tbName, &exist));
        }
      
        if (0 == exist) {
          TSWAP(pTask->msgCtx.lastOut, pTask->msgCtx.out);
          CTG_ERR_JRET(ctgGetTbMetaFromMnodeImpl(CTG_PARAMS_LIST(), pOut->dbFName, pOut->tbName, NULL, pTask));
        } else {
          taosMemoryFreeClear(pOut->tbMeta);
          
          SET_META_TYPE_CTABLE(pOut->metaType); 
        }
      }
      break;
    }
    default:
      ctgError("invalid reqType %d", reqType);
      CTG_ERR_JRET(TSDB_CODE_INVALID_MSG);
      break;
  }

  STableMetaOutput* pOut = (STableMetaOutput*)pTask->msgCtx.out;

  ctgUpdateTbMetaToCache(pCtg, pOut, false);

  if (CTG_IS_META_BOTH(pOut->metaType)) {
    memcpy(pOut->tbMeta, &pOut->ctbMeta, sizeof(pOut->ctbMeta));
  } else if (CTG_IS_META_CTABLE(pOut->metaType)) {
    SName stbName = *ctx->pName;
    strcpy(stbName.tname, pOut->tbName);
    SCtgTbMetaCtx stbCtx = {0};
    stbCtx.flag = ctx->flag;
    stbCtx.pName = &stbName;
    
    CTG_ERR_JRET(ctgReadTbMetaFromCache(pCtg, &stbCtx, &pOut->tbMeta));
    if (NULL == pOut->tbMeta) {
      ctgDebug("stb no longer exist, stbName:%s", stbName.tname);
      CTG_ERR_JRET(ctgRelaunchGetTbMetaTask(pTask));

      return TSDB_CODE_SUCCESS;
    }

    memcpy(pOut->tbMeta, &pOut->ctbMeta, sizeof(pOut->ctbMeta));
  }

  TSWAP(pTask->res, pOut->tbMeta);

_return:

  if (dbCache) {
    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }

  ctgHandleTaskEnd(pTask, code);

  CTG_RET(code);
}

int32_t ctgHandleGetDbVgRsp(SCtgTask* pTask, int32_t reqType, const SDataBuf *pMsg, int32_t rspCode) {
  int32_t code = 0;
  CTG_ERR_JRET(ctgProcessRspMsg(pTask->msgCtx.out, reqType, pMsg->pData, pMsg->len, rspCode, pTask->msgCtx.target));

  SCtgDbVgCtx* ctx = (SCtgDbVgCtx*)pTask->taskCtx;
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;

  switch (reqType) {
    case TDMT_MND_USE_DB: {
      SUseDbOutput* pOut = (SUseDbOutput*)pTask->msgCtx.out;

      CTG_ERR_JRET(ctgGenerateVgList(pCtg, pOut->dbVgroup->vgHash, (SArray**)&pTask->res));
      
      CTG_ERR_JRET(ctgPutUpdateVgToQueue(pCtg, ctx->dbFName, pOut->dbId, pOut->dbVgroup, false));
      pOut->dbVgroup = NULL;

      break;
    }
    default:
      ctgError("invalid reqType %d", reqType);
      CTG_ERR_JRET(TSDB_CODE_INVALID_MSG);
      break;
  }


_return:

  ctgHandleTaskEnd(pTask, code);

  CTG_RET(code);
}

int32_t ctgHandleGetTbHashRsp(SCtgTask* pTask, int32_t reqType, const SDataBuf *pMsg, int32_t rspCode) {
  int32_t code = 0;
  CTG_ERR_JRET(ctgProcessRspMsg(pTask->msgCtx.out, reqType, pMsg->pData, pMsg->len, rspCode, pTask->msgCtx.target));

  SCtgTbHashCtx* ctx = (SCtgTbHashCtx*)pTask->taskCtx;
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;

  switch (reqType) {
    case TDMT_MND_USE_DB: {
      SUseDbOutput* pOut = (SUseDbOutput*)pTask->msgCtx.out;

      pTask->res = taosMemoryMalloc(sizeof(SVgroupInfo));
      if (NULL == pTask->res) {
        CTG_ERR_JRET(TSDB_CODE_OUT_OF_MEMORY);
      }

      CTG_ERR_JRET(ctgGetVgInfoFromHashValue(pCtg, pOut->dbVgroup, ctx->pName, (SVgroupInfo*)pTask->res));
      
      CTG_ERR_JRET(ctgPutUpdateVgToQueue(pCtg, ctx->dbFName, pOut->dbId, pOut->dbVgroup, false));
      pOut->dbVgroup = NULL;

      break;
    }
    default:
      ctgError("invalid reqType %d", reqType);
      CTG_ERR_JRET(TSDB_CODE_INVALID_MSG);
      break;
  }


_return:

  ctgHandleTaskEnd(pTask, code);

  CTG_RET(code);
}


int32_t ctgAsyncRefreshTbMeta(SCtgTask *pTask) {
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;
  int32_t code = 0;
  SCtgTbMetaCtx* ctx = (SCtgTbMetaCtx*)pTask->taskCtx;

  if (CTG_FLAG_IS_SYS_DB(ctx->flag)) {
    ctgDebug("will refresh sys db tbmeta, tbName:%s", tNameGetTableName(ctx->pName));

    CTG_RET(ctgGetTbMetaFromMnodeImpl(CTG_PARAMS_LIST(), (char *)ctx->pName->dbname, (char *)ctx->pName->tname, NULL, pTask));
  }

  if (CTG_FLAG_IS_STB(ctx->flag)) {
    ctgDebug("will refresh tbmeta, supposed to be stb, tbName:%s", tNameGetTableName(ctx->pName));

    // if get from mnode failed, will not try vnode
    CTG_RET(ctgGetTbMetaFromMnode(CTG_PARAMS_LIST(), ctx->pName, NULL, pTask));
  }

  SCtgDBCache *dbCache = NULL;
  char dbFName[TSDB_DB_FNAME_LEN] = {0};
  tNameGetFullDbName(ctx->pName, dbFName);
  
  CTG_ERR_RET(ctgAcquireVgInfoFromCache(pCtg, dbFName, &dbCache));
  if (NULL == dbCache) {
    SVgroupInfo vgInfo = {0};
    CTG_ERR_RET(ctgGetVgInfoFromHashValue(pCtg, dbCache->vgInfo, ctx->pName, &vgInfo));

    ctgDebug("will refresh tbmeta, not supposed to be stb, tbName:%s, flag:%d", tNameGetTableName(ctx->pName), ctx->flag);

    CTG_ERR_JRET(ctgGetTbMetaFromVnode(CTG_PARAMS_LIST(), ctx->pName, &vgInfo, NULL, pTask));
  } else {
    SBuildUseDBInput input = {0};

    tstrncpy(input.db, dbFName, tListLen(input.db));
    input.vgVersion = CTG_DEFAULT_INVALID_VERSION;

    CTG_ERR_JRET(ctgGetDBVgInfoFromMnode(pCtg, pTrans, pMgmtEps, &input, NULL, pTask));
  }

_return:

  if (dbCache) {
    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }

  CTG_RET(code);
}

int32_t ctgLaunchGetTbMetaTask(SCtgTask *pTask) {
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;

  CTG_ERR_RET(ctgGetTbMetaFromCache(CTG_PARAMS_LIST(), (SCtgTbMetaCtx*)pTask->taskCtx, (STableMeta**)&pTask->res));
  if (pTask->res) {
    CTG_ERR_RET(ctgHandleTaskEnd(pTask, 0));
    return TSDB_CODE_SUCCESS;
  }

  CTG_ERR_RET(ctgAsyncRefreshTbMeta(pTask));

  return TSDB_CODE_SUCCESS;
}

int32_t ctgLaunchGetDbVgTask(SCtgTask *pTask) {
  int32_t code = 0;
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;
  SCtgDBCache *dbCache = NULL;
  SCtgDbVgCtx* pCtx = (SCtgDbVgCtx*)pTask->taskCtx;
  
  CTG_ERR_RET(ctgAcquireVgInfoFromCache(pCtg, pCtx->dbFName, &dbCache));
  if (NULL != dbCache) {
    CTG_ERR_JRET(ctgGenerateVgList(pCtg, dbCache->vgInfo->vgHash, (SArray**)&pTask->res));
    
    CTG_ERR_JRET(ctgHandleTaskEnd(pTask, 0));
  } else {
    SBuildUseDBInput input = {0};
    
    tstrncpy(input.db, pCtx->dbFName, tListLen(input.db));
    input.vgVersion = CTG_DEFAULT_INVALID_VERSION;
    
    CTG_ERR_RET(ctgGetDBVgInfoFromMnode(pCtg, pTrans, pMgmtEps, &input, NULL, pTask));
  }

_return:

  if (dbCache) {
    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }

  CTG_RET(code);
}

int32_t ctgLaunchGetTbHashTask(SCtgTask *pTask) {
  int32_t code = 0;
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;
  SCtgDBCache *dbCache = NULL;
  SCtgTbHashCtx* pCtx = (SCtgTbHashCtx*)pTask->taskCtx;
  
  CTG_ERR_RET(ctgAcquireVgInfoFromCache(pCtg, pCtx->dbFName, &dbCache));
  if (NULL != dbCache) {
    pTask->res = taosMemoryMalloc(sizeof(SVgroupInfo));
    if (NULL == pTask->res) {
      CTG_ERR_JRET(TSDB_CODE_OUT_OF_MEMORY);
    }
    CTG_ERR_JRET(ctgGetVgInfoFromHashValue(pCtg, dbCache->vgInfo, pCtx->pName, (SVgroupInfo*)pTask->res));
    
    CTG_ERR_JRET(ctgHandleTaskEnd(pTask, 0));
  } else {
    SBuildUseDBInput input = {0};
    
    tstrncpy(input.db, pCtx->dbFName, tListLen(input.db));
    input.vgVersion = CTG_DEFAULT_INVALID_VERSION;
    
    CTG_ERR_RET(ctgGetDBVgInfoFromMnode(pCtg, pTrans, pMgmtEps, &input, NULL, pTask));
  }

_return:

  if (dbCache) {
    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }

  CTG_RET(code);
}

int32_t ctgRelaunchGetTbMetaTask(SCtgTask *pTask) {
  ctgResetTbMetaTask(pTask);

  CTG_ERR_RET(ctgLaunchGetTbMetaTask(pTask));

  return TSDB_CODE_SUCCESS;
}


SCtgAsyncFps gCtgAsyncFps[] = {
  {ctgLaunchGetTbMetaTask, ctgHandleGetTbMetaRsp, ctgDumpTbMetaRes},
  {ctgLaunchGetDbVgTask,   ctgHandleGetDbVgRsp,   ctgDumpDbVgRes},
  {ctgLaunchGetTbHashTask, ctgHandleGetTbHashRsp, ctgDumpTbHashRes},
};


int32_t ctgMakeAsyncRes(SCtgJob *pJob) {
  int32_t code = 0;
  int32_t taskNum = taosArrayGetSize(pJob->pTasks);
  
  for (int32_t i = 0; i < taskNum; ++i) {
    SCtgTask *pTask = taosArrayGet(pJob->pTasks, i);
    CTG_ERR_JRET((*gCtgAsyncFps[pTask->type].dumpResFp)(pTask));
  }

  return TSDB_CODE_SUCCESS;

_return:

  taosArrayDestroy(pJob->jobRes.pTableMeta);
  pJob->jobRes.pTableMeta = NULL;
  taosArrayDestroy(pJob->jobRes.pUdfList);
  pJob->jobRes.pUdfList = NULL;
  
  CTG_RET(code);
}


int32_t ctgLaunchJob(SCtgJob *pJob) {
  int32_t taskNum = taosArrayGetSize(pJob->pTasks);
  
  for (int32_t i = 0; i < taskNum; ++i) {
    SCtgTask *pTask = taosArrayGet(pJob->pTasks, i);

    CTG_ERR_RET((*gCtgAsyncFps[pTask->type].launchFp)(pTask));
  }

  return TSDB_CODE_SUCCESS;
}



