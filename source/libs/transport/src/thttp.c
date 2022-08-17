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
#ifdef USE_UV
#include <uv.h>
#include "transComm.h"
#endif
// clang-format off
#include "zlib.h"
#include "thttp.h"
#include "taoserror.h"
#include "tlog.h"

static int32_t taosBuildHttpHeader(const char* server, int32_t contLen, char* pHead, int32_t headLen,
                                   EHttpCompFlag flag) {
  if (flag == HTTP_FLAT) {
    return snprintf(pHead, headLen,
                    "POST /report HTTP/1.1\n"
                    "Host: %s\n"
                    "Content-Type: application/json\n"
                    "Content-Length: %d\n\n",
                    server, contLen);
  } else if (flag == HTTP_GZIP) {
    return snprintf(pHead, headLen,
                    "POST /report HTTP/1.1\n"
                    "Host: %s\n"
                    "Content-Type: application/json\n"
                    "Content-Encoding: gzip\n"
                    "Content-Length: %d\n\n",
                    server, contLen);
  } else {
    return -1;
  }
}

int32_t taosCompressHttpRport(char* pSrc, int32_t srcLen) {
  int32_t code = -1;
  int32_t destLen = srcLen;
  void*   pDest = taosMemoryMalloc(destLen);

  if (pDest == NULL) {
    terrno = TSDB_CODE_OUT_OF_MEMORY;
    goto _OVER;
  }

  z_stream gzipStream = {0};
  gzipStream.zalloc = (alloc_func)0;
  gzipStream.zfree = (free_func)0;
  gzipStream.opaque = (voidpf)0;
  if (deflateInit2(&gzipStream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    terrno = TSDB_CODE_OUT_OF_MEMORY;
    goto _OVER;
  }

  gzipStream.next_in = (Bytef*)pSrc;
  gzipStream.avail_in = (uLong)srcLen;
  gzipStream.next_out = (Bytef*)pDest;
  gzipStream.avail_out = (uLong)(destLen);

  while (gzipStream.avail_in != 0 && gzipStream.total_out < (uLong)(destLen)) {
    if (deflate(&gzipStream, Z_FULL_FLUSH) != Z_OK) {
      terrno = TSDB_CODE_COMPRESS_ERROR;
      goto _OVER;
    }
  }

  if (gzipStream.avail_in != 0) {
    terrno = TSDB_CODE_COMPRESS_ERROR;
    goto _OVER;
  }

  int32_t err = 0;
  while (1) {
    if ((err = deflate(&gzipStream, Z_FINISH)) == Z_STREAM_END) {
      break;
    }
    if (err != Z_OK) {
      terrno = TSDB_CODE_COMPRESS_ERROR;
      goto _OVER;
    }
  }

  if (deflateEnd(&gzipStream) != Z_OK) {
    terrno = TSDB_CODE_COMPRESS_ERROR;
    goto _OVER;
  }

  if (gzipStream.total_out >= srcLen) {
    terrno = TSDB_CODE_COMPRESS_ERROR;
    goto _OVER;
  }

  code = 0;

_OVER:
  if (code == 0) {
    memcpy(pSrc, pDest, gzipStream.total_out);
    code = gzipStream.total_out;
  }

  taosMemoryFree(pDest);
  return code;
}

#ifdef USE_UV



typedef struct SHttpClient {
  uv_connect_t conn; 
  uv_tcp_t   tcp;
  uv_write_t req; 
  uv_buf_t   *buf;
  char     *addr;
  uint16_t port;
} SHttpClient;

static void destroyHttpClient(SHttpClient *cli) {
  taosMemoryFree(cli->buf);
  taosMemoryFree(cli->addr);
  taosMemoryFree(cli);
} 
static void clientCloseCb(uv_handle_t* handle) {
  SHttpClient *cli = handle->data;
  destroyHttpClient(cli);
} 
static void clientSentCb(uv_write_t* req, int32_t status) {
  SHttpClient *cli = req->data; 
  if (status != 0) {
    terrno = TAOS_SYSTEM_ERROR(status);
    uError("http-report failed to send data %s", uv_strerror(status));
  } else {
    uInfo("http-report succ to send data");
  }
  uv_close((uv_handle_t*)&cli->tcp, clientCloseCb);
  
}
static void clientConnCb(uv_connect_t* req, int32_t status) {
  SHttpClient *cli = req->data;
  if (status != 0) {
    terrno = TAOS_SYSTEM_ERROR(status);
    uError("http-report failed to conn to server, reason:%s, dst:%s:%d", uv_strerror(status), cli->addr, cli->port);
    uv_close((uv_handle_t*)&cli->tcp, clientCloseCb);
    return;
  }
  uv_write(&cli->req, (uv_stream_t *)&cli->tcp, cli->buf, 2, clientSentCb);
}


static int32_t taosBuildDstAddr(const char  *server, uint16_t port, struct sockaddr_in *dest) {
  uint32_t ipv4 = taosGetIpv4FromFqdn(server);
  if (ipv4 == 0xffffffff) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    uError("http-report failed to get http server:%s ip since %s", server, terrstr());
    return -1;
  }
  char ipv4Buf[128] = {0};

  tinet_ntoa(ipv4Buf, ipv4);
  uv_ip4_addr(ipv4Buf, port, dest);
  return 0;
  
} 
int32_t taosSendHttpReport(const char* server, uint16_t port, char* pCont, int32_t contLen, EHttpCompFlag flag) {
  struct sockaddr_in dest = {0};
  if (taosBuildDstAddr(server, port, &dest) < 0) {
    return -1;
  }
  if (flag == HTTP_GZIP) {
    int32_t dstLen = taosCompressHttpRport(pCont, contLen);
    if (dstLen > 0) {
      contLen = dstLen;
    } else {
      flag = HTTP_FLAT;
    }
  }
  terrno = 0;

  char    header[1024] = {0};
  int32_t headLen = taosBuildHttpHeader(server, contLen, header, sizeof(header), flag);

  uv_buf_t *wb = taosMemoryCalloc(2, sizeof(uv_buf_t));
  wb[0] = uv_buf_init((char*)header, headLen); // stack var 
  wb[1] = uv_buf_init((char*)pCont, contLen); //  heap var 

  SHttpClient *cli = taosMemoryCalloc(1, sizeof(SHttpClient)); 
  cli->conn.data = cli; 
  cli->tcp.data = cli;
  cli->req.data = cli;
  cli->buf = wb;
  cli->addr = tstrdup(server);
  cli->port = port;
  
  uv_loop_t* loop = uv_default_loop();
  uv_tcp_init(loop, &cli->tcp);
  // set up timeout to avoid stuck;
  int32_t fd = taosCreateSocketWithTimeout(TRANS_CONN_TIMEOUT);
  uv_tcp_open((uv_tcp_t*)&cli->tcp, fd);

  int32_t ret =uv_tcp_connect(&cli->conn, &cli->tcp, (const struct sockaddr*)&dest, clientConnCb); 
  if (ret != 0) {
    uError("http-report failed to connect to server, reason:%s, dst:%s:%d", uv_strerror(ret), cli->addr, cli->port);
    destroyHttpClient(cli);
  } 
  
  uv_run(loop, UV_RUN_DEFAULT);
  uv_loop_close(loop);
  return terrno;
}

#else
int32_t taosSendHttpReport(const char* server, uint16_t port, char* pCont, int32_t contLen, EHttpCompFlag flag) {
  int32_t     code = -1;
  TdSocketPtr pSocket = NULL;

  uint32_t ip = taosGetIpv4FromFqdn(server);
  if (ip == 0xffffffff) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    uError("failed to get http server:%s ip since %s", server, terrstr());
    goto SEND_OVER;
  }

  pSocket = taosOpenTcpClientSocket(ip, port, 0);
  if (pSocket == NULL) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    uError("failed to create http socket to %s:%u since %s", server, port, terrstr());
    goto SEND_OVER;
  }

  if (flag == HTTP_GZIP) {
    int32_t dstLen = taosCompressHttpRport(pCont, contLen);
    if (dstLen > 0) {
      contLen = dstLen;
    } else {
      flag = HTTP_FLAT;
    }
  }

  char    header[1024] = {0};
  int32_t headLen = taosBuildHttpHeader(server, contLen, header, sizeof(header), flag);
  if (taosWriteMsg(pSocket, header, headLen) < 0) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    uError("failed to send http header to %s:%u since %s", server, port, terrstr());
    goto SEND_OVER;
  }

  if (taosWriteMsg(pSocket, (void*)pCont, contLen) < 0) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    uError("failed to send http content to %s:%u since %s", server, port, terrstr());
    goto SEND_OVER;
  }

  // read something to avoid nginx error 499
  if (taosWriteMsg(pSocket, header, 10) < 0) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    uError("failed to receive response from %s:%u since %s", server, port, terrstr());
    goto SEND_OVER;
  }

  code = 0;

SEND_OVER:
  if (pSocket != NULL) {
    taosCloseSocket(&pSocket);
  }

  return code;
}

// clang-format on
#endif
