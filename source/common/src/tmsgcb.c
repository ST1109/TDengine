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

#define _DEFAULT_SOURCE
#include "tmsgcb.h"
#include "taoserror.h"

static SMsgCb defaultMsgCb;

void tmsgSetDefault(const SMsgCb* msgcb) { defaultMsgCb = *msgcb; }

int32_t tmsgPutToQueue(const SMsgCb* msgcb, EQueueType qtype, SRpcMsg* pMsg) {
  return (*msgcb->putToQueueFp)(msgcb->mgmt, qtype, pMsg);
}

int32_t tmsgGetQueueSize(const SMsgCb* msgcb, int32_t vgId, EQueueType qtype) {
  return (*msgcb->qsizeFp)(msgcb->mgmt, vgId, qtype);
}

int32_t tmsgSendReq(const SEpSet* epSet, SRpcMsg* pMsg) { return (*defaultMsgCb.sendReqFp)(epSet, pMsg); }

void tmsgSendRsp(SRpcMsg* pMsg) { return (*defaultMsgCb.sendRspFp)(pMsg); }

void tmsgSendRedirectRsp(SRpcMsg* pMsg, const SEpSet* pNewEpSet) { (*defaultMsgCb.sendRedirectRspFp)(pMsg, pNewEpSet); }

void tmsgRegisterBrokenLinkArg(SRpcMsg* pMsg) { (*defaultMsgCb.registerBrokenLinkArgFp)(pMsg); }

void tmsgReleaseHandle(SRpcHandleInfo* pHandle, int8_t type) { (*defaultMsgCb.releaseHandleFp)(pHandle, type); }

void tmsgReportStartup(const char* name, const char* desc) { (*defaultMsgCb.reportStartupFp)(name, desc); }