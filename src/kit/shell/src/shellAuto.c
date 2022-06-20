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

#define __USE_XOPEN
#include "os.h"
#include "tglobal.h"
#include "shell.h"
#include "shellCommand.h"
#include "tkey.h"
#include "tulog.h"
#include "shellAuto.h"
#include "tire.h"
#include "tthread.h"

// extern function
void insertChar(Command *cmd, char *c, int size);


typedef struct SAutoPtr {
  STire* p;
  int ref;
}SAutoPtr;

typedef struct SWord{
  int type ; // word type , see WT_ define
  char * word;
  int32_t len;
  struct SWord * next;
}SWord;

typedef struct {
  char * source;
  int32_t source_len; // valid data length in source
  int32_t count;
  SWord*  head;
  // matched information
  int32_t matchIndex;    // matched word index in words
  int32_t matchLen;     // matched length at matched word
}SWords;


SWords shellCommands[] = {
  {"alter database <db_name> [blocks] [cachelast] [comp] [keep] [replica] [quorum]", 0, 0, NULL},
  {"alter dnote <dnodeid> balance column", 0, 0, NULL},
  {"alter table <tb_name> [add column|modify column|drop column|change tag]", 0, 0, NULL},
  {"alter table modify column", 0, 0, NULL},
  {"alter topic", 0, 0, NULL},
  {"alter user <username> pass", 0, 0, NULL},
  {"alter user <username> privilege [read|write]", 0, 0, NULL},
  {"create dnode", 0, 0, NULL},
  {"create function", 0, 0, NULL},
  {"create table <...> tags", 0, 0, NULL},
  {"create table <...> as", 0, 0, NULL},
  {"create topic", 0, 0, NULL},
  {"create user <...> pass", 0, 0, NULL},
  {"compact vnode in", 0, 0, NULL},
  {"describe <stb_name>", 0, 0, NULL},
#ifdef TD_ENTERPRISE
  {"delete from <tb_name> where", 0, 0, NULL},
#endif
  {"drop database <db_name>", 0, 0, NULL},
  {"drop dnode <dnodeid>", 0, 0, NULL},
  {"drop function", 0, 0, NULL},
  {"drop topic", 0, 0, NULL},
  {"drop table <tb_name>", 0, 0, NULL},
  {"drop user <username>", 0, 0, NULL},
  {"kill connection", 0, 0, NULL},
  {"kill query", 0, 0, NULL},
  {"kill stream", 0, 0, NULL},
  {"select <expr> from <tb_name> where <keyword> [group by] [order by] [asc|desc] [limit] [offset] [slimit] [soffset] [interval] [sliding] [fill] [session] [state]", 0, 0, NULL},
  {"select <expr> union all select <expr>", 0, 0, NULL},
  {"select _block_dist() from <tb_name>", 0, 0, NULL},
  {"select client_version();", 0, 0, NULL},
  {"select current_user();", 0, 0, NULL},
  {"select database;", 0, 0, NULL},
  {"select server_version();", 0, 0, NULL},
  {"set max_binary_display_width", 0, 0, NULL},
  {"show create database <db_name>", 0, 0, NULL},
  {"show create stable <stb_name>", 0, 0, NULL},
  {"show create table <tb_name>", 0, 0, NULL},
  {"show connections;", 0, 0, NULL},
  {"show databases;", 0, 0, NULL},
  {"show dnodes;", 0, 0, NULL},
  {"show functions;", 0, 0, NULL},
  {"show modules;", 0, 0, NULL},
  {"show mnodes;", 0, 0, NULL},
  {"show queries;", 0, 0, NULL},
  {"show stables;", 0, 0, NULL},
  {"show stables like", 0, 0, NULL},
  {"show streams;", 0, 0, NULL},
  {"show scores;", 0, 0, NULL},
  {"show tables;", 0, 0, NULL},
  {"show tables like", 0, 0, NULL},
  {"show users;", 0, 0, NULL},
  {"show variables;", 0, 0, NULL},
  {"show vgroups;", 0, 0, NULL},
  {"insert into <tb_name> values", 0, 0, NULL},
  {"use <db_name>", 0, 0, NULL}
};

//
//  ------- gobal variant define ---------
//
int32_t firstMatchIndex = -1; // first match shellCommands index
int32_t lastMatchIndex  = -1; // last match shellCommands index
int32_t curMatchIndex   = -1; // current match shellCommands index
int32_t lastWordBytes   = -1; // printShow last word length 
bool    waitAutoFill    = false;


//
//   ----------- global var array define -----------
//
#define WT_VAR_DBNAME 0
#define WT_VAR_STABLE 1
#define WT_VAR_TABLE  2
#define WT_VAR_CNT    3

#define WT_TEXT       0xFF

// tire array
STire* tires[WT_VAR_CNT];
pthread_mutex_t tiresMutex;
//save thread handle obtain var name from db server
pthread_t* threads[WT_VAR_CNT];
// obtain var name  with sql from server
char varTypes[WT_VAR_CNT][64] = {
  "<db_name>",
  "<stb_name>",
  "<tb_name>"
};

char varSqls[WT_VAR_CNT][64] = {
  "show databases;",
  "show stables;",
  "show tables;"
};


// var words current cursor, if user press any one key except tab, cursorVar can be reset to -1
int cursorVar = -1;
bool varMode = false; // enter var names list mode

// define buffer for SWord->word for variant name used
char varName[1024] = "";

TAOS* varCon = NULL;
Command* varCmd = NULL;
SMatch* lastMatch = NULL; // save last match result 


//
//  -------------------  parse words --------------------------
//

#define SHELL_COMMAND_COUNT() (sizeof(shellCommands) / sizeof(SWords))

// get at
SWord * atWord(SWords * command, int32_t index) {
  SWord * word = command->head;
  for (int32_t i = 0; i < index; i++) {
    if (word == NULL)
      return NULL;
    word = word->next;
  }

  return word;
}

#define MATCH_WORD(x) atWord(x, x->matchIndex)

int wordType(const char* p, int32_t len) {
  for (int i = 0; i < WT_VAR_CNT; i++) {
    if (strncmp(p, varTypes[i], len) == 0)
        return i;
  }
  return WT_TEXT;
}

// add word
SWord * addWord(const char* p, int32_t len, bool pattern) {
  SWord* word = (SWord *) malloc(sizeof(SWord));
  memset(word, 0, sizeof(SWord));
  word->word = (char* )p;
  word->len  = len;

  // check format
  if (pattern) {
    word->type = wordType(p, len);
  } else {
    word->type = WT_TEXT;
  }

  return word;
}

// parse one command
void parseCommand(SWords * command, bool pattern) {
  char * p = command->source;
  int32_t start = 0;
  int32_t size  = command->source_len > 0 ? command->source_len : strlen(p);

  bool lastBlank = false;
  for (int i = 0; i <= size; i++) {
    if (p[i] == ' ' || i == size) {
      // check continue blank like '    '
      if (p[i] == ' ') {
        if (lastBlank) {
          start ++;
          continue;
        }
        if (i == 0) { // first blank
          lastBlank = true;
          start ++;
          continue;
        }
        lastBlank = true;
      } 

      // found split or string end , append word
      if (command->head == NULL) {
        command->head = addWord(p + start, i - start, pattern);
        command->count = 1;
      } else {
        SWord * word = command->head;
        while (word->next) {
          word = word->next;
        }
        word->next = addWord(p + start, i - start, pattern);
        command->count ++;
      }
      start = i + 1;
    } else {
      lastBlank = false;
    }
  }
}

// free Command
void freeCommand(SWords * command) {
  SWord * word = command->head;
  if (word == NULL) {
    return ;
  }

  // loop 
  while (word->next) {
    SWord * tmp = word;
    word = word->next;
    free(tmp);
  }
  free(word);
}

//
//  -------------------- shell auto ----------------
//

// init shell auto funciton , shell start call once 
bool shellAutoInit() {
  // command
  int32_t count = SHELL_COMMAND_COUNT();
  for (int32_t i = 0; i < count; i ++) {
    parseCommand(shellCommands + i, true);
  }

  // tires
  memset(tires, 0, sizeof(STire*) * WT_VAR_CNT);
  pthread_mutex_init(&tiresMutex, NULL);

  // threads
  memset(threads, 0, sizeof(pthread_t*) * WT_VAR_CNT);

  return true;
}

// exit shell auto funciton, shell exit call once
void shellAutoExit() {
  // free command
  int32_t count = SHELL_COMMAND_COUNT();
  for (int32_t i = 0; i < count; i ++) {
    freeCommand(shellCommands + i);
  }

  // free tires
  pthread_mutex_lock(&tiresMutex);
  for (int32_t i = 0; i < WT_VAR_CNT; i++) {
    if (tires[i]) {
      freeTire(tires[i]);
      tires[i] = NULL;
    } 
  }
  pthread_mutex_unlock(&tiresMutex);
  // destory
  pthread_mutex_destroy(&tiresMutex);

  // free threads
  for (int32_t i = 0; i < WT_VAR_CNT; i++) {
    if (threads[i]) {
      taosDestroyThread(threads[i]);
      threads[i] = NULL;
    } 
  }

  // free lastMatch
  if (lastMatch) {
    freeMatch(lastMatch);
    lastMatch = NULL;
  }
}

//
//  -------------------  auto ptr for tires --------------------------
//
bool setNewAuotPtr(int type, STire* pNew) {
  if (pNew == NULL)
    return false;

  pthread_mutex_lock(&tiresMutex);
  STire* pOld = tires[type];
  if (pOld != NULL) {
    // previous have value, release self ref count
    if (--pOld->ref == 0) {
      freeTire(pOld);
    }
  }

  // set new
  tires[type] = pNew;
  tires[type]->ref = 1;
  pthread_mutex_unlock(&tiresMutex);

  return true;
}

// get ptr
STire* getAutoPtr(int type) {
  if (tires[type] == NULL) {
    return NULL;
  }

  pthread_mutex_lock(&tiresMutex);
  tires[type]->ref++;
  pthread_mutex_unlock(&tiresMutex);

  return tires[type];
}

// put back tire to tires[type], if tire not equal tires[type].p, need free tire
void putBackAutoPtr(int type, STire* tire) {
  if (tire == NULL) {
    return ;
  }
  bool needFree = false;

  pthread_mutex_lock(&tiresMutex);
  if (tires[type] != tire) {
    //update by out,  can't put back , so free
    if (--tire->ref == 1) {
      // support multi thread getAuotPtr
      freeTire(tire);
    }
    
  } else {
    tires[type]->ref--;
    assert(tires[type]->ref > 0);
  }
  pthread_mutex_unlock(&tiresMutex);

  if (needFree) {
    freeTire(tire);
  }
  return ;
}



//
//  -------------------  var Word --------------------------
//

#define MAX_CACHED_CNT 100000 // max cached rows 10w
// write sql result to var name, return write rows cnt
int writeVarNames(int type, TAOS_RES* tres) {
  // fetch row 
  TAOS_ROW row = taos_fetch_row(tres);
  if (row == NULL) {
    return 0;
  }

  // sql
  int colIdx = 0;
  
  int num_fields = taos_num_fields(tres);
  TAOS_FIELD *fields = taos_fetch_fields(tres);

  // check type
  if (colIdx >= num_fields || fields[colIdx].type != TSDB_DATA_TYPE_BINARY) {
    return 0;
  }

  // create new tires
  STire* tire = createTire();

  // enum rows
  char name[1024];
  int numOfRows = 0;
  do {
    int32_t* lengths = taos_fetch_lengths(tres);
    int32_t bytes = lengths[colIdx];
    memcpy(name, row[colIdx], bytes);
    name[bytes] = 0;  //set string end 
    // insert to tire
    insertWord(tire, name);

    if (++numOfRows > MAX_CACHED_CNT ) {
      break;
    }

    row = taos_fetch_row(tres);
  } while (row != NULL);

  // replace old tire
  setNewAuotPtr(type, tire);

  return numOfRows;
}

void firstMatchCommand(TAOS * con, Command * cmd);
// obtain var thread from db server 
void* varObtainThread(void* param) {
  int type = *(int* )param;
  free(param);

  if (varCon == NULL || type >= WT_VAR_CNT) {
    return NULL;
  }

  TAOS_RES* pSql = taos_query_h(varCon, varSqls[type], NULL);
  if (taos_errno(pSql)) {
    taos_free_result(pSql);
    return NULL;
  }

  // write var names from pSql
  int cnt = writeVarNames(type, pSql);

  // free sql
  taos_free_result(pSql);

  // check need call auto tab 
  if (cnt > 0 && waitAutoFill) {
    // press tab key by program
    firstMatchCommand(varCon, varCmd);
  }

  return NULL;
}

// only match next one word from all match words
bool matchNextPrefix(STire* tire, char* pre, char* output) {
  bool ret = false;
  SMatch* match = NULL;

  // re-use last result
  if (lastMatch) {
    if (strcmp(pre, lastMatch->pre) == 0) {
      // same pre
      match = lastMatch;
    }
  }

  if (match == NULL) {
    // not same with last result
    if (pre[0] == 0) {
      // EMPTY PRE
      match = enumAll(tire);
    } else {
      // NOT EMPTY
      match = matchPrefix(tire, pre, NULL);
    }
    
    // save to lastMatch
    if (match) {
      if (lastMatch)
        freeMatch(lastMatch);
      lastMatch = match;
    }
  }

  // check valid
  if (match == NULL || match->head == NULL) {
    // no one matched
    return false;
  }

  if (cursorVar == -1) {
    // first
    strcpy(output, match->head->word);
    cursorVar = 0;
    return true;    
  }

  // according to cursorVar , calculate next one
  int i = 0;
  SMatchNode* item = match->head;
  while (item) {
    if (i == cursorVar + 1) {
      // found next position ok
      strcpy(output, item->word);
      ret = true;
      if (item->next == NULL) {
        // match last item, reset cursorVar to head
        cursorVar = -1;
      } else {
        cursorVar = i;
      }

      break;
    }

    // check end item
    if (item->next == NULL) {
      // if cursorVar > var list count, return last and reset cursorVar
      strcpy(output, item->word);
      ret = true;
      cursorVar = -1;
    }

    // move next
    item = item->next;
    i++;
  }

  return ret;
}

// search pre word from tire tree, return value is global buffer,so need not free
char* tireSearchWord(int type, char* pre) {
  if (type == WT_TEXT) {
    return NULL;
  }

  pthread_mutex_lock(&tiresMutex);

  // check need obtain from server
  if (tires[type] == NULL) {
    waitAutoFill = true;
    // need async obtain var names from db sever
    if (threads[type] != NULL) {
      if (taosThreadRunning(threads[type])) {
        // thread running , need not obtain again, return 
        pthread_mutex_unlock(&tiresMutex);
        return NULL;
      }
      // destroy previous thread handle for new create thread handle
      taosDestroyThread(threads[type]);
      threads[type] = NULL;
    }
  
    // create new
    void * param = malloc(sizeof(int));
    *((int* )param) = type;
    threads[type] = taosCreateThread(varObtainThread, param);
    pthread_mutex_unlock(&tiresMutex);
    return NULL;
  }
  pthread_mutex_unlock(&tiresMutex);

  // can obtain var names from local
  STire* tire = getAutoPtr(type);
  if (tire == NULL) {
    return NULL;
  }

  // auto tab function is single thread operate, so here set global varName is appropriate
  bool ret = matchNextPrefix(tire, pre, varName);
  // used finish, put back pointer to autoptr array
  putBackAutoPtr(type, tire);

  if (ret) {
    return varName;
  }

  return NULL;
}

// match var word, word1 is pattern , word2 is input from shell 
bool matchVarWord(SWord* word1, SWord* word2) {
  // search input word from tire tree 
  char pre[256];
  memcpy(pre, word2->word, word2->len);
  pre[word2->len] = 0;
  char* word = tireSearchWord(word1->type, pre);
  if (word == NULL) {
    // not found or word1->type variable list not obtain from server, return not match
    return false;
  }

  // save
  word1->word = word;
  word1->len  = strlen(word);
  
  return true;
}

//
//  -------------------  match words --------------------------
//


// compare command cmd1 come from shellCommands , cmd2 come from user input
int32_t compareCommand(SWords * cmd1, SWords * cmd2) {
  SWord * word1 = cmd1->head;
  SWord * word2 = cmd2->head;

  if (word1 == NULL || word2 == NULL) {
    return -1;
  }

  for (int32_t i = 0; i < cmd1->count; i++) {
    if (word1->type == WT_TEXT) {
      // WT_TEXT match
      if (word1->len == word2->len) {
        if (strncasecmp(word1->word, word2->word, word1->len) != 0)
          return -1; 
      } else if (word1->len < word2->len) {
        return -1;
      } else {
        // word1->len > word2->len
        if (strncasecmp(word1->word, word2->word, word2->len) == 0) {
          cmd1->matchIndex = i;
          cmd1->matchLen = word2->len;
          return i;
        } else {
          return -1;
        }
      }
    } else {
      // WT_VAR auto match any one word
      if (word2->next == NULL) { // input words last one
        if (matchVarWord(word1, word2)) {
          cmd1->matchIndex = i;
          cmd1->matchLen = word2->len;
          varMode = true;
          return i;
        }
        return -1;
      }
    }

    // move next
    word1 = word1->next;
    word2 = word2->next;
    if (word1 == NULL || word2 == NULL) {
      return -1;
    }
  }

  return -1;
}

// match command
SWords * matchCommand(SWords * input, bool continueSearch) {
  int32_t count = SHELL_COMMAND_COUNT();
  for (int32_t i = 0; i < count; i ++) {
    SWords * shellCommand = shellCommands + i;
    if (continueSearch && lastMatchIndex != -1 && i <= lastMatchIndex) {
      // new match must greate than lastMatchIndex
      if (varMode && i == lastMatchIndex) {
        // do nothing, var match on lastMatchIndex
      } else {
        continue;
      }
    }

    // command is large
    if (input->count > shellCommand->count ) {
      continue;
    }

    // compare
    int32_t index = compareCommand(shellCommand, input);
    if (index != -1) {
      if (firstMatchIndex == -1)
        firstMatchIndex = i;
      curMatchIndex = i;
      return &shellCommands[i];
    }
  }

  // not match
  return NULL;
}

//
//  -------------------  print screen --------------------------
//


// show screen
void printScreen(TAOS * con, Command * cmd, SWords * match) {
  // modify Command
  if (firstMatchIndex == -1 || curMatchIndex == -1) {
    // no match
    return ;
  }

  // first tab press 
  const char * str = NULL;
  int strLen = 0; 

  if (firstMatchIndex == curMatchIndex && lastWordBytes == -1) {
    // first press tab
    SWord * word = MATCH_WORD(match);
    str = word->word + match->matchLen;
    strLen = word->len - match->matchLen;
    lastMatchIndex = firstMatchIndex;
    lastWordBytes = word->len;
  } else {
    if (lastWordBytes == -1)
      return ;

    // continue to press tab any times
    int count = lastWordBytes;

    // delete last match word length
    int size = 0;
    int width = 0;
    clearScreen(cmd->endOffset + prompt_size, cmd->screenOffset + prompt_size);
    while (--count >= 0 && cmd->cursorOffset > 0) {
      getPrevCharSize(cmd->command, cmd->cursorOffset, &size, &width);
      memmove(cmd->command + cmd->cursorOffset - size, cmd->command + cmd->cursorOffset,
              cmd->commandSize - cmd->cursorOffset);
      cmd->commandSize -= size;
      cmd->cursorOffset -= size;
      cmd->screenOffset -= width;
      cmd->endOffset -= width;
    }

    SWord * word = MATCH_WORD(match);
    str = word->word;
    strLen = word->len;
    // set current to last
    lastMatchIndex = curMatchIndex;
    lastWordBytes = word->len;
  }
  
  // insert new
  insertChar(cmd, (char *)str, strLen);
}


// main key press tab
void firstMatchCommand(TAOS * con, Command * cmd) {
  // parse command
  SWords* input = (SWords *)malloc(sizeof(SWords));
  memset(input, 0, sizeof(SWords));
  input->source = cmd->command;
  input->source_len = cmd->commandSize;
  parseCommand(input, false);

  // if have many , default match first, if press tab again , switch to next
  curMatchIndex  = -1;
  lastMatchIndex = -1;
  SWords * match = matchCommand(input, true);
  if (match == NULL) {
    // not match , nothing to do
    freeCommand(input);
    return ;
  }

  // print to screen
  printScreen(con, cmd, match);
  freeCommand(input);
}

// create input source
void createInputFromFirst(SWords* input, SWords * firstMatch) {
  //
  // if next pressTabKey , input context come from firstMatch, set matched length with source_len
  //
  input->source = (char*)malloc(1024);
  memset((void* )input->source, 0, 1024);

  SWord * word = firstMatch->head;

  // source_len = full match word->len + half match with firstMatch->matchLen  
  for (int i = 0; i < firstMatch->matchIndex && word; i++) {
    // combine source from each word
    strncpy(input->source + input->source_len, word->word, word->len);
    strcat(input->source, " "); // append blank splite
    input->source_len += word->len + 1; // 1 is blank length
    // move next
    word = word->next;
  }
  // appand half matched word for last
  if (word) {
    strncpy(input->source + input->source_len, word->word, firstMatch->matchLen);
    input->source_len += firstMatch->matchLen;
  }
}

// user press Tabkey again is named next
void nextMatchCommand(TAOS * con, Command * cmd, SWords * firstMatch) {
  if (firstMatch == NULL || firstMatch->head == NULL) {
    return ;
  }
  SWords* input = (SWords *)malloc(sizeof(SWords));
  memset(input, 0, sizeof(SWords));

  // create input from firstMatch
  createInputFromFirst(input, firstMatch);

  // parse input
  parseCommand(input, false);

  // if have many , default match first, if press tab again , switch to next
  SWords * match = matchCommand(input, true);
  if (match == NULL) {
    // if not match , reset all index
    firstMatchIndex = -1;
    curMatchIndex   = -1;
    match = matchCommand(input, false);
    if (match == NULL) {
      freeCommand(input);
      return ;
    }
  }

  // print to screen
  printScreen(con, cmd, match);

  // free
  if (input->source) {
    free(input->source);
    input->source = NULL;
  }
  freeCommand(input);
}

// show help
void showHelp(TAOS * con, Command * cmd) {
  
}

// main key press tab
void pressTabKey(TAOS * con, Command * cmd) {
  // check 
  if (cmd->commandSize == 0) { 
    // empty
    showHelp(con, cmd);
    return ;
  } 

  // save connection to global
  varCon = con;
  varCmd = cmd;

  if (firstMatchIndex == -1) {
    firstMatchCommand(con, cmd);
  } else {
    nextMatchCommand(con, cmd, &shellCommands[firstMatchIndex]);
  }
}

// press othr key
void pressOtherKey(char c) {
  // reset global variant
  firstMatchIndex = -1;
  lastMatchIndex  = -1;
  curMatchIndex   = -1;
  lastWordBytes   = -1;

  // var names
  cursorVar    = -1;
  varMode      = false;
  waitAutoFill = false;

  if (lastMatch) {
    freeMatch(lastMatch);
    lastMatch = NULL;
  }
}

// callback autotab module after shell sql execute
void callbackAutoTab(char* sqlstr, TAOS* pSql) {
  // if sqlstr length > 32, return 
  int i =0;
  while(sqlstr[i++] != 0) {
    if (i > 32) {
      return ;
    }
  }

  char *p = sqlstr;
  while(p[0] == ' ') {
    p++;
  }

  int len;
  for (i = 0; i < WT_VAR_CNT; i ++) {
    len = strlen(varSqls[i]) - 1;
    if (strncmp(p, varSqls[i], len) == 0) {
      writeVarNames(i, pSql);
      break;
    }
  }
}
