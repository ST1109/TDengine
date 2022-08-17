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

#include "syncReplication.h"
#include "syncIndexMgr.h"
#include "syncMessage.h"
#include "syncRaftCfg.h"
#include "syncRaftEntry.h"
#include "syncRaftLog.h"
#include "syncRaftStore.h"
#include "syncSnapshot.h"
#include "syncUtil.h"

// TLA+ Spec
// AppendEntries(i, j) ==
//    /\ i /= j
//    /\ state[i] = Leader
//    /\ LET prevLogIndex == nextIndex[i][j] - 1
//           prevLogTerm == IF prevLogIndex > 0 THEN
//                              log[i][prevLogIndex].term
//                          ELSE
//                              0
//           \* Send up to 1 entry, constrained by the end of the log.
//           lastEntry == Min({Len(log[i]), nextIndex[i][j]})
//           entries == SubSeq(log[i], nextIndex[i][j], lastEntry)
//       IN Send([mtype          |-> AppendEntriesRequest,
//                mterm          |-> currentTerm[i],
//                mprevLogIndex  |-> prevLogIndex,
//                mprevLogTerm   |-> prevLogTerm,
//                mentries       |-> entries,
//                \* mlog is used as a history variable for the proof.
//                \* It would not exist in a real implementation.
//                mlog           |-> log[i],
//                mcommitIndex   |-> Min({commitIndex[i], lastEntry}),
//                msource        |-> i,
//                mdest          |-> j])
//    /\ UNCHANGED <<serverVars, candidateVars, leaderVars, logVars>>
//
int32_t syncNodeAppendEntriesPeers(SSyncNode* pSyncNode) {
  ASSERT(pSyncNode->state == TAOS_SYNC_STATE_LEADER);

  syncIndexMgrLog2("==syncNodeAppendEntriesPeers== pNextIndex", pSyncNode->pNextIndex);
  syncIndexMgrLog2("==syncNodeAppendEntriesPeers== pMatchIndex", pSyncNode->pMatchIndex);
  logStoreSimpleLog2("==syncNodeAppendEntriesPeers==", pSyncNode->pLogStore);

  int32_t ret = 0;
  for (int i = 0; i < pSyncNode->peersNum; ++i) {
    SRaftId* pDestId = &(pSyncNode->peersId[i]);

    // set prevLogIndex
    SyncIndex nextIndex = syncIndexMgrGetIndex(pSyncNode->pNextIndex, pDestId);

    SyncIndex preLogIndex = nextIndex - 1;

    // set preLogTerm
    SyncTerm preLogTerm = 0;
    if (preLogIndex >= SYNC_INDEX_BEGIN) {
      SSyncRaftEntry* pPreEntry = pSyncNode->pLogStore->getEntry(pSyncNode->pLogStore, preLogIndex);
      ASSERT(pPreEntry != NULL);

      preLogTerm = pPreEntry->term;
      syncEntryDestory(pPreEntry);
    }

    // batch optimized
    // SyncIndex lastIndex = syncUtilMinIndex(pSyncNode->pLogStore->getLastIndex(pSyncNode->pLogStore), nextIndex);

    SyncAppendEntries* pMsg = NULL;
    SSyncRaftEntry*    pEntry = pSyncNode->pLogStore->getEntry(pSyncNode->pLogStore, nextIndex);
    if (pEntry != NULL) {
      pMsg = syncAppendEntriesBuild(pEntry->bytes, pSyncNode->vgId);
      ASSERT(pMsg != NULL);

      // add pEntry into msg
      uint32_t len;
      char*    serialized = syncEntrySerialize(pEntry, &len);
      ASSERT(len == pEntry->bytes);
      memcpy(pMsg->data, serialized, len);

      taosMemoryFree(serialized);
      syncEntryDestory(pEntry);

    } else {
      // maybe overflow, send empty record
      pMsg = syncAppendEntriesBuild(0, pSyncNode->vgId);
      ASSERT(pMsg != NULL);
    }

    ASSERT(pMsg != NULL);
    pMsg->srcId = pSyncNode->myRaftId;
    pMsg->destId = *pDestId;
    pMsg->term = pSyncNode->pRaftStore->currentTerm;
    pMsg->prevLogIndex = preLogIndex;
    pMsg->prevLogTerm = preLogTerm;
    pMsg->commitIndex = pSyncNode->commitIndex;

    syncAppendEntriesLog2("==syncNodeAppendEntriesPeers==", pMsg);

    // send AppendEntries
    syncNodeAppendEntries(pSyncNode, pDestId, pMsg);
    syncAppendEntriesDestroy(pMsg);
  }

  return ret;
}

int32_t syncNodeAppendEntriesOnePeer(SSyncNode* pSyncNode, SRaftId* pDestId, SyncIndex nextIndex) {
  int32_t ret = 0;

  // pre index, pre term
  SyncIndex preLogIndex = syncNodeGetPreIndex(pSyncNode, nextIndex);
  SyncTerm  preLogTerm = syncNodeGetPreTerm(pSyncNode, nextIndex);
  if (preLogTerm == SYNC_TERM_INVALID) {
    SyncIndex newNextIndex = syncNodeGetLastIndex(pSyncNode) + 1;
    // SyncIndex newNextIndex = nextIndex + 1;

    syncIndexMgrSetIndex(pSyncNode->pNextIndex, pDestId, newNextIndex);
    syncIndexMgrSetIndex(pSyncNode->pMatchIndex, pDestId, SYNC_INDEX_INVALID);
    sError("vgId:%d, sync get pre term error, nextIndex:%" PRId64 ", update next-index:%" PRId64
           ", match-index:%d, raftid:%" PRId64,
           pSyncNode->vgId, nextIndex, newNextIndex, SYNC_INDEX_INVALID, pDestId->addr);
    return -1;
  }

  // entry pointer array
  SSyncRaftEntry* entryPArr[SYNC_MAX_BATCH_SIZE];
  memset(entryPArr, 0, sizeof(entryPArr));

  // get entry batch
  int32_t   getCount = 0;
  SyncIndex getEntryIndex = nextIndex;
  for (int32_t i = 0; i < pSyncNode->pRaftCfg->batchSize; ++i) {
    SSyncRaftEntry* pEntry = NULL;
    int32_t         code = pSyncNode->pLogStore->syncLogGetEntry(pSyncNode->pLogStore, getEntryIndex, &pEntry);
    if (code == 0) {
      ASSERT(pEntry != NULL);
      entryPArr[i] = pEntry;
      getCount++;
      getEntryIndex++;

    } else {
      break;
    }
  }

  // event log
  do {
    char     logBuf[128];
    char     host[64];
    uint16_t port;
    syncUtilU642Addr(pDestId->addr, host, sizeof(host), &port);
    snprintf(logBuf, sizeof(logBuf), "build batch:%d for %s:%d", getCount, host, port);
    syncNodeEventLog(pSyncNode, logBuf);
  } while (0);

  // build msg
  SyncAppendEntriesBatch* pMsg = syncAppendEntriesBatchBuild(entryPArr, getCount, pSyncNode->vgId);
  ASSERT(pMsg != NULL);

  // free entries
  for (int32_t i = 0; i < pSyncNode->pRaftCfg->batchSize; ++i) {
    SSyncRaftEntry* pEntry = entryPArr[i];
    if (pEntry != NULL) {
      syncEntryDestory(pEntry);
      entryPArr[i] = NULL;
    }
  }

  // prepare msg
  pMsg->srcId = pSyncNode->myRaftId;
  pMsg->destId = *pDestId;
  pMsg->term = pSyncNode->pRaftStore->currentTerm;
  pMsg->prevLogIndex = preLogIndex;
  pMsg->prevLogTerm = preLogTerm;
  pMsg->commitIndex = pSyncNode->commitIndex;
  pMsg->privateTerm = 0;
  pMsg->dataCount = getCount;

  // send msg
  syncNodeAppendEntriesBatch(pSyncNode, pDestId, pMsg);

  // speed up
  if (pMsg->dataCount > 0 && pSyncNode->commitIndex - pMsg->prevLogIndex > SYNC_SLOW_DOWN_RANGE) {
    ret = 1;

#if 0
      do {
        char     logBuf[128];
        char     host[64];
        uint16_t port;
        syncUtilU642Addr(pDestId->addr, host, sizeof(host), &port);
        snprintf(logBuf, sizeof(logBuf), "maybe speed up for %s:%d, pre-index:%ld", host, port, pMsg->prevLogIndex);
        syncNodeEventLog(pSyncNode, logBuf);
      } while (0);
#endif
  }

  syncAppendEntriesBatchDestroy(pMsg);

  return ret;
}

bool syncNodeWaitProgress(SSyncNode* pSyncNode) { return pSyncNode->enableWaitProgress; }

void syncNodeEnableWaitProgress(SSyncNode* pSyncNode) {
  pSyncNode->enableWaitProgress = true;
  pSyncNode->waitProgressStartTime = taosGetTimestampMs();
}

void syncNodeDisableWaitProgress(SSyncNode* pSyncNode) { pSyncNode->enableWaitProgress = false; }

bool syncNodeWaitProgressTimeout(SSyncNode* pSyncNode) {
  return (pSyncNode->enableWaitProgress &&
          taosGetTimestampMs() - pSyncNode->waitProgressStartTime > SYNC_MAX_PROGRESS_WAIT_MS);
}

bool syncNodeNeedWaitProgress(SSyncNode* pSyncNode, SRaftId* pDestId1Ret, SyncIndex* pNextIndex1Ret,
                              SRaftId* pDestId2Ret, SyncIndex* pNextIndex2Ret) {
  SyncIndex diff;
  if (pSyncNode->peersNum > 0) {
    ASSERT(pSyncNode->peersNum == 2);

    SRaftId*  pDestId1 = &(pSyncNode->peersId[0]);
    SyncIndex nextIndex1 = syncIndexMgrGetIndex(pSyncNode->pNextIndex, pDestId1);

    SRaftId*  pDestId2 = &(pSyncNode->peersId[1]);
    SyncIndex nextIndex2 = syncIndexMgrGetIndex(pSyncNode->pNextIndex, pDestId2);

    diff = nextIndex2 - nextIndex1;
    if (diff < 0) {
      diff *= -1;
    }

    if (diff > SYNC_MAX_PROGRESS_OVER_RANGE) {
      do {
        char logBuf[128];
        snprintf(logBuf, sizeof(logBuf), "wait progress over range, next1:%ld, next2:%ld, diff:%ld", nextIndex1,
                 nextIndex2, diff);
        syncNodeEventLog(pSyncNode, logBuf);
      } while (0);

      return false;
    }

    if (diff > SYNC_MAX_PROGRESS_RANGE) {
      *pDestId1Ret = *pDestId1;
      *pDestId2Ret = *pDestId2;

      if (nextIndex1 < nextIndex2) {
        *pNextIndex1Ret = nextIndex1;
        *pNextIndex2Ret = nextIndex1 + SYNC_MAX_PROGRESS_RANGE - 1;
      } else {
        *pNextIndex2Ret = nextIndex2;
        *pNextIndex1Ret = nextIndex2 + SYNC_MAX_PROGRESS_RANGE - 1;
      }

      do {
        char logBuf[128];
        snprintf(logBuf, sizeof(logBuf), "wait progress, next1:%ld, next2:%ld, diff:%ld, update-n1:%ld, update-n2:%ld",
                 nextIndex1, nextIndex2, diff, *pNextIndex1Ret, *pNextIndex2Ret);
        syncNodeEventLog(pSyncNode, logBuf);
      } while (0);

      return true;
    }
  }

  return false;
}

static int32_t syncNodeDoAppendEntriesPeers(SSyncNode* pSyncNode) {
  int32_t ret = 0;
  for (int i = 0; i < pSyncNode->peersNum; ++i) {
    SRaftId* pDestId = &(pSyncNode->peersId[i]);

    // next index
    SyncIndex nextIndex = syncIndexMgrGetIndex(pSyncNode->pNextIndex, pDestId);
    ret = syncNodeAppendEntriesOnePeer(pSyncNode, pDestId, nextIndex);
  }
  return ret;
}

int32_t syncNodeAppendEntriesPeersSnapshot2(SSyncNode* pSyncNode) {
  if (pSyncNode->state != TAOS_SYNC_STATE_LEADER) {
    return -1;
  }

  int32_t ret = 0;

  SRaftId   destId1 = {0};
  SyncIndex nextIndex1 = SYNC_INDEX_INVALID;
  SRaftId   destId2 = {0};
  SyncIndex nextIndex2 = SYNC_INDEX_INVALID;

  bool needWaitProgress = syncNodeNeedWaitProgress(pSyncNode, &destId1, &nextIndex1, &destId2, &nextIndex2);
  if (needWaitProgress) {
    if (syncNodeWaitProgress(pSyncNode)) {
      // already wait progress
      if (syncNodeWaitProgressTimeout(pSyncNode)) {
        // timeout, replicate normally
        syncNodeDisableWaitProgress(pSyncNode);
        ret = syncNodeDoAppendEntriesPeers(pSyncNode);

      } else {
        // replicate wait progress
        ret = syncNodeAppendEntriesOnePeer(pSyncNode, &destId1, nextIndex1);
        ret = syncNodeAppendEntriesOnePeer(pSyncNode, &destId2, nextIndex2);
      }

    } else {
      // replicate wait progress
      syncNodeEnableWaitProgress(pSyncNode);
      ret = syncNodeAppendEntriesOnePeer(pSyncNode, &destId1, nextIndex1);
      ret = syncNodeAppendEntriesOnePeer(pSyncNode, &destId2, nextIndex2);
    }

  } else {
    ret = syncNodeDoAppendEntriesPeers(pSyncNode);
  }

  return ret;
}

#if 0
int32_t syncNodeAppendEntriesPeersSnapshot2(SSyncNode* pSyncNode) {
  if (pSyncNode->state != TAOS_SYNC_STATE_LEADER) {
    return -1;
  }

  int32_t ret = 0;
  for (int i = 0; i < pSyncNode->peersNum; ++i) {
    SRaftId* pDestId = &(pSyncNode->peersId[i]);

    // next index
    SyncIndex nextIndex = syncIndexMgrGetIndex(pSyncNode->pNextIndex, pDestId);

    // pre index, pre term
    SyncIndex preLogIndex = syncNodeGetPreIndex(pSyncNode, nextIndex);
    SyncTerm  preLogTerm = syncNodeGetPreTerm(pSyncNode, nextIndex);
    if (preLogTerm == SYNC_TERM_INVALID) {
      SyncIndex newNextIndex = syncNodeGetLastIndex(pSyncNode) + 1;
      // SyncIndex newNextIndex = nextIndex + 1;

      syncIndexMgrSetIndex(pSyncNode->pNextIndex, pDestId, newNextIndex);
      syncIndexMgrSetIndex(pSyncNode->pMatchIndex, pDestId, SYNC_INDEX_INVALID);
      sError("vgId:%d, sync get pre term error, nextIndex:%" PRId64 ", update next-index:%" PRId64
             ", match-index:%d, raftid:%" PRId64,
             pSyncNode->vgId, nextIndex, newNextIndex, SYNC_INDEX_INVALID, pDestId->addr);
      return -1;
    }

    // entry pointer array
    SSyncRaftEntry* entryPArr[SYNC_MAX_BATCH_SIZE];
    memset(entryPArr, 0, sizeof(entryPArr));

    // get entry batch
    int32_t   getCount = 0;
    SyncIndex getEntryIndex = nextIndex;
    for (int32_t i = 0; i < pSyncNode->pRaftCfg->batchSize; ++i) {
      SSyncRaftEntry* pEntry = NULL;
      int32_t         code = pSyncNode->pLogStore->syncLogGetEntry(pSyncNode->pLogStore, getEntryIndex, &pEntry);
      if (code == 0) {
        ASSERT(pEntry != NULL);
        entryPArr[i] = pEntry;
        getCount++;
        getEntryIndex++;

      } else {
        break;
      }
    }

    // event log
    do {
      char     logBuf[128];
      char     host[64];
      uint16_t port;
      syncUtilU642Addr(pDestId->addr, host, sizeof(host), &port);
      snprintf(logBuf, sizeof(logBuf), "build batch:%d for %s:%d", getCount, host, port);
      syncNodeEventLog(pSyncNode, logBuf);
    } while (0);

    // build msg
    SyncAppendEntriesBatch* pMsg = syncAppendEntriesBatchBuild(entryPArr, getCount, pSyncNode->vgId);
    ASSERT(pMsg != NULL);

    // free entries
    for (int32_t i = 0; i < pSyncNode->pRaftCfg->batchSize; ++i) {
      SSyncRaftEntry* pEntry = entryPArr[i];
      if (pEntry != NULL) {
        syncEntryDestory(pEntry);
        entryPArr[i] = NULL;
      }
    }

    // prepare msg
    pMsg->srcId = pSyncNode->myRaftId;
    pMsg->destId = *pDestId;
    pMsg->term = pSyncNode->pRaftStore->currentTerm;
    pMsg->prevLogIndex = preLogIndex;
    pMsg->prevLogTerm = preLogTerm;
    pMsg->commitIndex = pSyncNode->commitIndex;
    pMsg->privateTerm = 0;
    pMsg->dataCount = getCount;

    // send msg
    syncNodeAppendEntriesBatch(pSyncNode, pDestId, pMsg);

    // speed up
    if (pMsg->dataCount > 0 && pSyncNode->commitIndex - pMsg->prevLogIndex > SYNC_SLOW_DOWN_RANGE) {
      ret = 1;

#if 0
      do {
        char     logBuf[128];
        char     host[64];
        uint16_t port;
        syncUtilU642Addr(pDestId->addr, host, sizeof(host), &port);
        snprintf(logBuf, sizeof(logBuf), "maybe speed up for %s:%d, pre-index:%ld", host, port, pMsg->prevLogIndex);
        syncNodeEventLog(pSyncNode, logBuf);
      } while (0);
#endif
    }

    syncAppendEntriesBatchDestroy(pMsg);
  }

  return ret;
}
#endif

int32_t syncNodeAppendEntriesPeersSnapshot(SSyncNode* pSyncNode) {
  ASSERT(pSyncNode->state == TAOS_SYNC_STATE_LEADER);

  syncIndexMgrLog2("begin append entries peers pNextIndex:", pSyncNode->pNextIndex);
  syncIndexMgrLog2("begin append entries peers pMatchIndex:", pSyncNode->pMatchIndex);
  logStoreSimpleLog2("begin append entries peers LogStore:", pSyncNode->pLogStore);

  int32_t ret = 0;
  for (int i = 0; i < pSyncNode->peersNum; ++i) {
    SRaftId* pDestId = &(pSyncNode->peersId[i]);

    // next index
    SyncIndex nextIndex = syncIndexMgrGetIndex(pSyncNode->pNextIndex, pDestId);

    // pre index, pre term
    SyncIndex preLogIndex = syncNodeGetPreIndex(pSyncNode, nextIndex);
    SyncTerm  preLogTerm = syncNodeGetPreTerm(pSyncNode, nextIndex);
    if (preLogTerm == SYNC_TERM_INVALID) {
      SyncIndex newNextIndex = syncNodeGetLastIndex(pSyncNode) + 1;
      // SyncIndex newNextIndex = nextIndex + 1;

      syncIndexMgrSetIndex(pSyncNode->pNextIndex, pDestId, newNextIndex);
      syncIndexMgrSetIndex(pSyncNode->pMatchIndex, pDestId, SYNC_INDEX_INVALID);
      sError("vgId:%d, sync get pre term error, nextIndex:%" PRId64 ", update next-index:%" PRId64
             ", match-index:%d, raftid:%" PRId64,
             pSyncNode->vgId, nextIndex, newNextIndex, SYNC_INDEX_INVALID, pDestId->addr);

      return -1;
    }

    // prepare entry
    SyncAppendEntries* pMsg = NULL;

    SSyncRaftEntry* pEntry;
    int32_t         code = pSyncNode->pLogStore->syncLogGetEntry(pSyncNode->pLogStore, nextIndex, &pEntry);

    if (code == 0) {
      ASSERT(pEntry != NULL);

      pMsg = syncAppendEntriesBuild(pEntry->bytes, pSyncNode->vgId);
      ASSERT(pMsg != NULL);

      // add pEntry into msg
      uint32_t len;
      char*    serialized = syncEntrySerialize(pEntry, &len);
      ASSERT(len == pEntry->bytes);
      memcpy(pMsg->data, serialized, len);

      taosMemoryFree(serialized);
      syncEntryDestory(pEntry);

    } else {
      if (terrno == TSDB_CODE_WAL_LOG_NOT_EXIST) {
        // no entry in log
        pMsg = syncAppendEntriesBuild(0, pSyncNode->vgId);
        ASSERT(pMsg != NULL);

      } else {
        syncNodeLog3("", pSyncNode);
        ASSERT(0);
      }
    }

    // prepare msg
    ASSERT(pMsg != NULL);
    pMsg->srcId = pSyncNode->myRaftId;
    pMsg->destId = *pDestId;
    pMsg->term = pSyncNode->pRaftStore->currentTerm;
    pMsg->prevLogIndex = preLogIndex;
    pMsg->prevLogTerm = preLogTerm;
    pMsg->commitIndex = pSyncNode->commitIndex;
    pMsg->privateTerm = 0;
    // pMsg->privateTerm = syncIndexMgrGetTerm(pSyncNode->pNextIndex, pDestId);

    // send msg
    syncNodeAppendEntries(pSyncNode, pDestId, pMsg);
    syncAppendEntriesDestroy(pMsg);
  }

  return ret;
}

int32_t syncNodeReplicate(SSyncNode* pSyncNode, bool isTimer) {
  // start replicate
  int32_t ret = 0;

  switch (pSyncNode->pRaftCfg->snapshotStrategy) {
    case SYNC_STRATEGY_NO_SNAPSHOT:
      ret = syncNodeAppendEntriesPeers(pSyncNode);
      break;

    case SYNC_STRATEGY_STANDARD_SNAPSHOT:
      ret = syncNodeAppendEntriesPeersSnapshot(pSyncNode);
      break;

    case SYNC_STRATEGY_WAL_FIRST:
      ret = syncNodeAppendEntriesPeersSnapshot2(pSyncNode);
      break;

    default:
      ret = syncNodeAppendEntriesPeers(pSyncNode);
      break;
  }

  // start delay
  int64_t timeNow = taosGetTimestampMs();
  int64_t startDelay = timeNow - pSyncNode->startTime;

  // replicate delay
  int64_t replicateDelay = timeNow - pSyncNode->lastReplicateTime;
  pSyncNode->lastReplicateTime = timeNow;

  if (ret > 0 && isTimer && startDelay > SYNC_SPEED_UP_AFTER_MS) {
    // speed up replicate
    int32_t ms =
        pSyncNode->heartbeatTimerMS < SYNC_SPEED_UP_HB_TIMER ? pSyncNode->heartbeatTimerMS : SYNC_SPEED_UP_HB_TIMER;
    syncNodeRestartNowHeartbeatTimerMS(pSyncNode, ms);

#if 0
    do {
      char logBuf[128];
      snprintf(logBuf, sizeof(logBuf), "replicate speed up");
      syncNodeEventLog(pSyncNode, logBuf);
    } while (0);
#endif

  } else {
    syncNodeRestartHeartbeatTimer(pSyncNode);

#if 0
    do {
      char logBuf[128];
      snprintf(logBuf, sizeof(logBuf), "replicate slow down");
      syncNodeEventLog(pSyncNode, logBuf);
    } while (0);
#endif
  }

  return ret;
}

int32_t syncNodeAppendEntries(SSyncNode* pSyncNode, const SRaftId* destRaftId, const SyncAppendEntries* pMsg) {
  int32_t ret = 0;
  syncLogSendAppendEntries(pSyncNode, pMsg, "");

  SRpcMsg rpcMsg;
  syncAppendEntries2RpcMsg(pMsg, &rpcMsg);
  syncNodeSendMsgById(destRaftId, pSyncNode, &rpcMsg);
  return ret;
}

int32_t syncNodeAppendEntriesBatch(SSyncNode* pSyncNode, const SRaftId* destRaftId,
                                   const SyncAppendEntriesBatch* pMsg) {
  syncLogSendAppendEntriesBatch(pSyncNode, pMsg, "");

  SRpcMsg rpcMsg;
  syncAppendEntriesBatch2RpcMsg(pMsg, &rpcMsg);
  syncNodeSendMsgById(destRaftId, pSyncNode, &rpcMsg);
  return 0;
}