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

int32_t ctgGetQnodeListFromMnode(CTG_PARAMS, SArray *out, bool async) {
  char *msg = NULL;
  int32_t msgLen = 0;

  ctgDebug("try to get qnode list from mnode, mgmtEpInUse:%d", pMgmtEps->inUse);

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_MND_QNODE_LIST)](NULL, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build qnode list msg failed, error:%s", tstrerror(code));
    CTG_ERR_RET(code);
  }
  
  SRpcMsg rpcMsg = {
      .msgType = TDMT_MND_QNODE_LIST,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};

  rpcSendRecv(pTrans, (SEpSet*)pMgmtEps, &rpcMsg, &rpcRsp);
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    ctgError("error rsp for qnode list, error:%s", tstrerror(rpcRsp.code));
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_MND_QNODE_LIST)](out, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process qnode list rsp failed, error:%s", tstrerror(rpcRsp.code));
    CTG_ERR_RET(code);
  }

  ctgDebug("Got qnode list from mnode, listNum:%d", (int32_t)taosArrayGetSize(out));

  return TSDB_CODE_SUCCESS;
}


int32_t ctgGetDBVgInfoFromMnode(CTG_PARAMS, SBuildUseDBInput *input, SUseDbOutput *out, bool async) {
  char *msg = NULL;
  int32_t msgLen = 0;

  ctgDebug("try to get db vgInfo from mnode, dbFName:%s", input->db);

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_MND_USE_DB)](input, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build use db msg failed, code:%x, db:%s", code, input->db);
    CTG_ERR_RET(code);
  }
  
  SRpcMsg rpcMsg = {
      .msgType = TDMT_MND_USE_DB,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};

  rpcSendRecv(pTrans, (SEpSet*)pMgmtEps, &rpcMsg, &rpcRsp);
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    ctgError("error rsp for use db, error:%s, db:%s", tstrerror(rpcRsp.code), input->db);
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_MND_USE_DB)](out, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process use db rsp failed, code:%x, db:%s", code, input->db);
    CTG_ERR_RET(code);
  }

  ctgDebug("Got db vgInfo from mnode, dbFName:%s", input->db);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgGetDBCfgFromMnode(CTG_PARAMS, const char *dbFName, SDbCfgInfo *out, bool async) {
  char *msg = NULL;
  int32_t msgLen = 0;

  ctgDebug("try to get db cfg from mnode, dbFName:%s", dbFName);

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_MND_GET_DB_CFG)]((void *)dbFName, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build get db cfg msg failed, code:%x, db:%s", code, dbFName);
    CTG_ERR_RET(code);
  }
  
  SRpcMsg rpcMsg = {
      .msgType = TDMT_MND_GET_DB_CFG,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};

  rpcSendRecv(pTrans, (SEpSet*)pMgmtEps, &rpcMsg, &rpcRsp);
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    ctgError("error rsp for get db cfg, error:%s, db:%s", tstrerror(rpcRsp.code), dbFName);
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_MND_GET_DB_CFG)](out, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process get db cfg rsp failed, code:%x, db:%s", code, dbFName);
    CTG_ERR_RET(code);
  }

  ctgDebug("Got db cfg from mnode, dbFName:%s", dbFName);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgGetIndexInfoFromMnode(CTG_PARAMS, const char *indexName, SIndexInfo *out, bool async) {
  char *msg = NULL;
  int32_t msgLen = 0;

  ctgDebug("try to get index from mnode, indexName:%s", indexName);

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_MND_GET_INDEX)]((void *)indexName, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build get index msg failed, code:%x, db:%s", code, indexName);
    CTG_ERR_RET(code);
  }
  
  SRpcMsg rpcMsg = {
      .msgType = TDMT_MND_GET_INDEX,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};

  rpcSendRecv(pTrans, (SEpSet*)pMgmtEps, &rpcMsg, &rpcRsp);
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    ctgError("error rsp for get index, error:%s, indexName:%s", tstrerror(rpcRsp.code), indexName);
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_MND_GET_INDEX)](out, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process get index rsp failed, code:%x, indexName:%s", code, indexName);
    CTG_ERR_RET(code);
  }

  ctgDebug("Got index from mnode, indexName:%s", indexName);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgGetUdfInfoFromMnode(CTG_PARAMS, const char *funcName, SFuncInfo **out, bool async) {
  char *msg = NULL;
  int32_t msgLen = 0;

  ctgDebug("try to get udf info from mnode, funcName:%s", funcName);

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_MND_RETRIEVE_FUNC)]((void *)funcName, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build get udf msg failed, code:%x, db:%s", code, funcName);
    CTG_ERR_RET(code);
  }
  
  SRpcMsg rpcMsg = {
      .msgType = TDMT_MND_RETRIEVE_FUNC,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};

  rpcSendRecv(pTrans, (SEpSet*)pMgmtEps, &rpcMsg, &rpcRsp);
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    if (TSDB_CODE_MND_FUNC_NOT_EXIST == rpcRsp.code) {
      ctgDebug("funcName %s not exist in mnode", funcName);
      taosMemoryFreeClear(*out);
      CTG_RET(TSDB_CODE_SUCCESS);
    }
    
    ctgError("error rsp for get udf, error:%s, funcName:%s", tstrerror(rpcRsp.code), funcName);
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_MND_RETRIEVE_FUNC)](*out, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process get udf rsp failed, code:%x, funcName:%s", code, funcName);
    CTG_ERR_RET(code);
  }

  ctgDebug("Got udf from mnode, funcName:%s", funcName);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgGetUserDbAuthFromMnode(CTG_PARAMS, const char *user, SGetUserAuthRsp *authRsp, bool async) {
  char *msg = NULL;
  int32_t msgLen = 0;

  ctgDebug("try to get user auth from mnode, user:%s", user);

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_MND_GET_USER_AUTH)]((void *)user, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build get user auth msg failed, code:%x, db:%s", code, user);
    CTG_ERR_RET(code);
  }
  
  SRpcMsg rpcMsg = {
      .msgType = TDMT_MND_GET_USER_AUTH,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};

  rpcSendRecv(pTrans, (SEpSet*)pMgmtEps, &rpcMsg, &rpcRsp);
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    ctgError("error rsp for get user auth, error:%s, user:%s", tstrerror(rpcRsp.code), user);
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_MND_GET_USER_AUTH)](authRsp, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process get user auth rsp failed, code:%x, user:%s", code, user);
    CTG_ERR_RET(code);
  }

  ctgDebug("Got user auth from mnode, user:%s", user);

  return TSDB_CODE_SUCCESS;
}


int32_t ctgGetTbMetaFromMnodeImpl(CTG_PARAMS, char *dbFName, char* tbName, STableMetaOutput* output, bool async) {
  SBuildTableMetaInput bInput = {.vgId = 0, .dbFName = dbFName, .tbName = tbName};
  char *msg = NULL;
  SEpSet *pVnodeEpSet = NULL;
  int32_t msgLen = 0;

  ctgDebug("try to get table meta from mnode, dbFName:%s, tbName:%s", dbFName, tbName);

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_MND_TABLE_META)](&bInput, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build mnode stablemeta msg failed, code:%x", code);
    CTG_ERR_RET(code);
  }

  SRpcMsg rpcMsg = {
      .msgType = TDMT_MND_TABLE_META,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};

  rpcSendRecv(pTrans, (SEpSet*)pMgmtEps, &rpcMsg, &rpcRsp);
  
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    if (CTG_TABLE_NOT_EXIST(rpcRsp.code)) {
      SET_META_TYPE_NULL(output->metaType);
      ctgDebug("stablemeta not exist in mnode, dbFName:%s, tbName:%s", dbFName, tbName);
      return TSDB_CODE_SUCCESS;
    }
    
    ctgError("error rsp for stablemeta from mnode, code:%s, dbFName:%s, tbName:%s", tstrerror(rpcRsp.code), dbFName, tbName);
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_MND_TABLE_META)](output, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process mnode stablemeta rsp failed, code:%x, dbFName:%s, tbName:%s", code, dbFName, tbName);
    CTG_ERR_RET(code);
  }

  ctgDebug("Got table meta from mnode, dbFName:%s, tbName:%s", dbFName, tbName);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgGetTbMetaFromMnode(CTG_PARAMS, const SName* pTableName, STableMetaOutput* output, bool async) {
  char dbFName[TSDB_DB_FNAME_LEN];
  tNameGetFullDbName(pTableName, dbFName);

  return ctgGetTbMetaFromMnodeImpl(CTG_PARAMS_LIST(), dbFName, (char *)pTableName->tname, output, async);
}

int32_t ctgGetTbMetaFromVnode(CTG_PARAMS, const SName* pTableName, SVgroupInfo *vgroupInfo, STableMetaOutput* output, bool async) {
  char dbFName[TSDB_DB_FNAME_LEN];
  tNameGetFullDbName(pTableName, dbFName);

  ctgDebug("try to get table meta from vnode, dbFName:%s, tbName:%s", dbFName, tNameGetTableName(pTableName));

  SBuildTableMetaInput bInput = {.vgId = vgroupInfo->vgId, .dbFName = dbFName, .tbName = (char *)tNameGetTableName(pTableName)};
  char *msg = NULL;
  int32_t msgLen = 0;

  int32_t code = queryBuildMsg[TMSG_INDEX(TDMT_VND_TABLE_META)](&bInput, &msg, 0, &msgLen);
  if (code) {
    ctgError("Build vnode tablemeta msg failed, code:%x, dbFName:%s, tbName:%s", code, dbFName, tNameGetTableName(pTableName));
    CTG_ERR_RET(code);
  }

  SRpcMsg rpcMsg = {
      .msgType = TDMT_VND_TABLE_META,
      .pCont   = msg,
      .contLen = msgLen,
  };

  if (async) {
    CTG_RET(ctgAsyncSendMsg());
  }

  SRpcMsg rpcRsp = {0};
  rpcSendRecv(pTrans, &vgroupInfo->epSet, &rpcMsg, &rpcRsp);
  
  if (TSDB_CODE_SUCCESS != rpcRsp.code) {
    if (CTG_TABLE_NOT_EXIST(rpcRsp.code)) {
      SET_META_TYPE_NULL(output->metaType);
      ctgDebug("tablemeta not exist in vnode, dbFName:%s, tbName:%s", dbFName, tNameGetTableName(pTableName));
      return TSDB_CODE_SUCCESS;
    }
  
    ctgError("error rsp for table meta from vnode, code:%s, dbFName:%s, tbName:%s", tstrerror(rpcRsp.code), dbFName, tNameGetTableName(pTableName));
    CTG_ERR_RET(rpcRsp.code);
  }

  code = queryProcessMsgRsp[TMSG_INDEX(TDMT_VND_TABLE_META)](output, rpcRsp.pCont, rpcRsp.contLen);
  if (code) {
    ctgError("Process vnode tablemeta rsp failed, code:%s, dbFName:%s, tbName:%s", tstrerror(code), dbFName, tNameGetTableName(pTableName));
    CTG_ERR_RET(code);
  }

  ctgDebug("Got table meta from vnode, dbFName:%s, tbName:%s", dbFName, tNameGetTableName(pTableName));
  return TSDB_CODE_SUCCESS;
}


int32_t ctg



