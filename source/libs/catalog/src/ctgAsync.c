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

SCtgAsyncFps gCtgAsyncFps[] = {
  {ctgLaunchGetTbMetaTask, ctgHandleGetTbMetaRsp},
  {ctgLaunchGetDbVgTask,   ctgHandleGetDbVgRsp},
};


int32_t ctgInitGetTbMetaTask(SCtgJob *pJob, int32_t taskIdx, SName *name) {
  SCtgTask *pTask = taosArrayGet(pJob->pTasks, taskIdx);

  pTask->type = CTG_TASK_GET_TB_META;
  pTask->taskId = taskIdx;
  pTask->pJob = pJob;

  pTask->ctx = taosMemoryCalloc(1, sizeof(SCtgTbMetaCtx));
  if (NULL == pTask->ctx) {
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }

  SCtgTbMetaCtx* ctx = pTask->ctx;
  ctx->pName = name;
  ctx->flag = CTG_FLAG_UNKNOWN_STB;

  return TSDB_CODE_SUCCESS;
}

int32_t ctgInitJob(CTG_PARAMS, SCtgJob** job, uint64_t reqId, const SCatalogReq* pReq, void* fp, void* param) {
  int32_t code = 0;
  int32_t tbNum = (int32_t)taosArrayGetSize(pReq->pTableName);
  int32_t udfNum = (int32_t)taosArrayGetSize(pReq->pUdf);
  int32_t taskNum = tbNum + udfNum;
  if (taskNum <= 0) {
    ctgError("empty input for job");
    CTG_ERR_RET(TSDB_CODE_CTG_INVALID_INPUT);
  }
  
  *job = taosMemoryCalloc(1, sizeof(SCtgJob));
  if (NULL == *job) {
    ctgError("calloc %d failed", sizeof(SCtgJob));
    CTG_ERR_RET(TSDB_CODE_OUT_OF_MEMORY);
  }

  SCtgJob *pJob = *job;
  pJob->queryId = reqId;
  pJob->pCtg    = pCtg;
  pJob->pTrans    = pTrans;
  pJob->pMgmtEps    = pMgmtEps;
  pJob->pTasks = taosArrayInit(taskNum, sizeof(SCtgTask));
  
  if (NULL == pJob->pTasks) {
    ctgError("taosArrayInit %d tasks failed", taskNum);
    CTG_ERR_JRET(TSDB_CODE_OUT_OF_MEMORY);
  }

  int32_t taskIdx = 0;
  for (int32_t i = 0; i < tbNum; ++i) {
    SName *name = taosArrayGet(pReq->pTableName, i);
    CTG_ERR_JRET(ctgInitGetTbMetaTask(pJob, taskIdx++, name));
  }

  return TSDB_CODE_SUCCESS;

_return:

  taosMemoryFreeClear(*job);

  CTG_RET(code);
}

int32_t ctgAsyncRefreshTbMeta(SCtgTask *pTask) {
  SVgroupInfo vgroupInfo = {0};
  int32_t code = 0;
  SCtgTbMetaCtx* ctx = (SCtgTbMetaCtx*)pTask->ctx;

  if (CTG_FLAG_IS_SYS_DB(ctx->flag)) {
    ctgDebug("will refresh sys db tbmeta, tbName:%s", tNameGetTableName(ctx->pName));

    CTG_RET(ctgGetTbMetaFromMnodeImpl(CTG_PARAMS_LIST(), (char *)ctx->pName->dbname, (char *)ctx->pName->tname, NULL, pTask));
  }

  if (CTG_FLAG_IS_STB(ctx->flag)) {
    ctgDebug("will refresh tbmeta, supposed to be stb, tbName:%s", tNameGetTableName(ctx->pName));

    // if get from mnode failed, will not try vnode
    CTG_RET(ctgGetTbMetaFromMnode(CTG_PARAMS_LIST(), ctx->pName, output, NULL));
  }

  CTG_ERR_RET(ctgAcquireVgInfoFromCache(pCtg, dbFName, dbCache, &inCache));
  if (inCache) {
    CTG_ERR_RET(ctgGetVgInfoFromHashValue(pCtg, dbCache->vgInfo, ctx->pName, pVgroup));

    ctgDebug("will refresh tbmeta, not supposed to be stb, tbName:%s, flag:%d", tNameGetTableName(ctx->pName), ctx->flag);

    // if get from vnode failed or no table meta, will not try mnode
    CTG_ERR_JRET(ctgGetTbMetaFromVnode(CTG_PARAMS_LIST(), ctx->pName, &vgroupInfo, output, NULL));
  } else {
    SBuildUseDBInput input = {0};

    tstrncpy(input.db, dbFName, tListLen(input.db));
    input.vgVersion = CTG_DEFAULT_INVALID_VERSION;

    CTG_RET(ctgGetDBVgInfoFromMnode(pCtg, pTrans, pMgmtEps, &input, NULL, pTask));
  }

  return TSDB_CODE_SUCCESS;
}

int32_t ctgLaunchGetTbMetaTask(SCtgTask *pTask) {
  SCatalog* pCtg = pTask->pJob->pCtg; 
  void *pTrans = pTask->pJob->pTrans; 
  const SEpSet* pMgmtEps = pTask->pJob->pMgmtEps;

  CTG_ERR_RET(ctgGetTbMetaFromCache(CTG_PARAMS_LIST(), (SCtgTbMetaCtx*)pTask->ctx, (STableMeta**)&pTask->res));
  if (pTask->res) {
    return TSDB_CODE_SUCCESS;
  }

  CTG_ERR_RET(ctgAsyncRefreshTbMeta(pTask));

  return TSDB_CODE_SUCCESS;
}

int32_t ctgLaunchJob(SCtgJob *pJob) {
  int32_t taskNum = taosArrayGetSize(pJob->pTasks);
  
  for (int32_t i = 0; i < taskNum; ++i) {
    SCtgTask *pTask = taosArrayGet(pJob->pTasks, i);

    CTG_ERR_RET((*gCtgAsyncFps[pTask->type].launchFp)(pTask));
  }

  return TSDB_CODE_SUCCESS;
}



