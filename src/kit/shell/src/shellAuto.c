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

// extern function
void insertChar(Command *cmd, char *c, int size);

typedef struct SWord{
  const char * word;
  int32_t len;
  struct SWord * next;
}SWord;

typedef struct {
  const char * source;
  int32_t source_len; // valid data length in source
  int32_t count;
  SWord*  head;
  // matched information
  int32_t matchIndex;    // matched word index in words
  int32_t matchLen;     // matched length at matched word
}SWords;

SWords shellCommands[] = {
  {"describe", 0, 0, NULL},
  {"show databases", 0, 0, NULL},
  {"show tables", 0, 0, NULL},
  {"show stables", 0, 0, NULL},
  {"use", 0, 0, NULL}
};

int32_t firstMatchIndex = -1; // first match shellCommands index
int32_t lastMatchIndex  = -1; // last match shellCommands index

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

// add word
SWord * addWord(const char* p, int32_t len) {
  SWord* word = (SWord *) malloc(sizeof(SWord));
  memset(word, 0, sizeof(SWord));
  word->word = p;
  word->len  = len;
  
  return word;
}

// parse one command
void parseCommand(SWords * command) {
  const char * p = command->source;
  int32_t start = 0;
  int32_t size  = command->source_len > 0 ? command->source_len : strlen(p);

  bool lastBlank = false;
  for (int i = 0; i <= size; i++) {
    if (p[i] == ' ' || i == size) {
      // check continue blank like '    '
      if(p[i] == ' ') {
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
        command->head = addWord(p + start, i - start);
        command->count = 1;
      } else {
        SWord * word = command->head;
        while(word->next) {
          word = word->next;
        }
        word->next = addWord(p + start, i - start);
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

// init shell auto funciton , shell start call once 
bool shellAutoInit() {
  int32_t count = SHELL_COMMAND_COUNT();
  for (int32_t i = 0; i < count; i ++) {
    parseCommand(shellCommands + i);
  }

  return true;
}

// exit shell auto funciton, shell exit call once
void shellAutoExit() {
  // free command
  int32_t count = SHELL_COMMAND_COUNT();
  for (int32_t i = 0; i < count; i ++) {
    freeCommand(shellCommands + i);
  }
}

// compare command cmd1 come from shellCommands , cmd2 come from user input
int32_t compareCommand(SWords * cmd1, SWords * cmd2) {
  SWord * word1 = cmd1->head;
  SWord * word2 = cmd2->head;
  for (int32_t i = 0; i < cmd1->count; i++) {

    if(word1->len == word2->len) {
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

    // move next
    word1 = word1->next;
    word2 = word2->next;
    if(word1 == NULL && word2 == NULL) {
      return -1;
    }
  }

  return -1;
}

// match command
SWords * matchCommand(SWords * command) {
  int32_t count = SHELL_COMMAND_COUNT();
  for (int32_t i = 0; i < count; i ++) {
    SWords * cmd1 = shellCommands + i;
    if (lastMatchIndex != -1 && i <= lastMatchIndex) {
      // new match must greate than lastMatchIndex
      continue;
    }

    // command is large
    if(command->count > cmd1->count ) {
      continue;
    }

    // compare
    int32_t index = compareCommand(cmd1, command);
    if (index != -1) {
      if (firstMatchIndex == -1)
        firstMatchIndex = i;
      lastMatchIndex = i;
      return &shellCommands[i];
    }
  }

  // not match
  return NULL;
}

// show screen
void printScreen(TAOS * con, Command * cmd, SWords * match) {
  // modify Command
  if (firstMatchIndex == -1) {
    // no match
    return ;
  }

  // first tab press 
  const char * str = NULL;
  int strLen = 0; 

  if (firstMatchIndex == lastMatchIndex) {
    // first press tab
    SWord * word = MATCH_WORD(match);
    str = word->word + match->matchLen;
    strLen = word->len - match->matchLen;
  } else {
    // continue to press tab any times
    SWords * last = &shellCommands[lastMatchIndex];
    int count = MATCH_WORD(last)->len;

    // delete last match word
    int size = 0;
    int width = 0;
    while(--count >= 0) {
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
  }
  
  // insert new
  insertChar(cmd, (char *)str, strLen);
}

// show help
void showHelp(TAOS * con, Command * cmd) {

}

// main key press tab
void pressTabKey(TAOS * con, Command * cmd) {
  // check 
  if(cmd->commandSize == 0) { 
    // empty
    showHelp(con, cmd);
    return ;
  } 

  // parse command
  SWords* command = (SWords *)malloc(sizeof(SWords));
  memset(command, 0, sizeof(SWords));
  command->source = cmd->command;
  command->source_len = cmd->commandSize;
  parseCommand(command);

  // if have many , default match first, if press tab again , switch to next
  SWords * match = matchCommand(command);
  if (match == NULL) {
    // not match , nothing to do
    freeCommand(command);
    return ;
  }

  // print to screen
  printScreen(con, cmd, match);
  freeCommand(command);
}

// press othr key
void pressOtherKey(char c) {
  // reset global variant
  firstMatchIndex = -1;
  lastMatchIndex  = -1;
}
