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
#include "tire.h"

// ----------- interface -------------

// create prefix search tree
STire* createTire() {
     STire* tire = malloc(sizeof(STire));
     memset(tire, 0, sizeof(STire));
     tire->ref = 1; // init is 1
     return tire;
}

// free tire node
void freeTireNode(STireNode* node) {
    if (node == NULL)
       return ;
  
    // nest free sub node on array d 
    for (int i = 0; i < CHAR_CNT; i++) {
        freeTireNode(node->d[i]);
    }

    // free self
    tfree(node);
}

// destroy prefix search tree
void freeTire(STire* tire) {
    // free nodes
    for (int i = 0; i < CHAR_CNT; i++) {
        freeTireNode(tire->root.d[i]);
    }

    // free tire
    tfree(tire);
}

// insert a new word 
bool insertWord(STire* tire, char* word) {
    int m  = 0;
    int len = strlen(word);
    if (len >= MAX_WORD_LEN) {
        return false;
    }

    STireNode** node = (STireNode** )&tire->root.d;
    for (int i = 0; i < len; i++) {
        m = word[i] - FIRST_ASCII;
        if (m < 0 || m >= CHAR_CNT) {
            return false;
        }

        if (node[m] == NULL) {
            // no pointer
            STireNode* p = (STireNode* )tmalloc(sizeof(STireNode));
            memset(p, 0, sizeof(STireNode));
            p->end = (i == len - 1); // end flag
            node[m] = p;
        } else {
            // not null, do nothing
        }

        // move to next node
        node = (STireNode** )&node[m]->d;
    }

    // add count
    tire->count += 1;
    return true;
}

// add a new word 
bool deleteWord(STire* tire, char* word) {
    int m  = 0;
    int len = strlen(word);
    bool del = false;
    if (len >= MAX_WORD_LEN) {
        return false;
    }

    STireNode** node = (STireNode** )&tire->root.d;
    for (int i = 0; i < len; i++) {
        m = word[i] - FIRST_ASCII;
        if (m < 0 || m >= CHAR_CNT) {
            return false;
        }

        if (node[m] == NULL) {
            // no found
            return false;
        } else {
            // not null
            if(i == len - 1) {
                // this is last, only set end false , not free node
                node[m]->end = false;
                del = true;
                break;
            }
        }

        // move to next node
        node = (STireNode** )&node[m]->d;
    }

    // reduce count
    if (del) {
        tire->count -= 1;
    }
    
    return del;
}

void addWordToMatch(SMatch* match, char* word){
    // malloc new
    SMatchNode* node = (SMatchNode* )tmalloc(sizeof(SMatchNode));
    memset(node, 0, sizeof(SMatchNode));
    strcpy(node->word, word);

    // append to match
    if (match->head == NULL) {
        match->head = match->tail = node;
    } else {
        match->tail->next = node;
        match->tail = node;
    }
    match->count += 1;
}

// enum all words from node
void enumAllWords(STireNode** node,  char* prefix, SMatch* match) {
    STireNode * c;
    char word[CHAR_CNT];
    int len = strlen(prefix);
    for (int i = 0; i < CHAR_CNT; i++) {
        c = node[i];
        
        if (c == NULL) {
            // chain end node
           continue; 
        } else {
            // combine word string
            memset(word, 0, sizeof(word));
            strcpy(word, prefix);
            word[len] =  FIRST_ASCII + i; // append current char

            // chain middle node
            if (c->end) {
                // have end flag
                addWordToMatch(match, word); 
            }
            // nested call next layer
            enumAllWords((STireNode** )&c->d, word, match);
        }
    }
}

// match prefix words, if match is not NULL , put all item to match and return match
SMatch* matchPrefix(STire* tire, char* prefix, SMatch* match) {
    SMatch* root = match;
    int m  = 0;
    STireNode* c = 0;
    int len = strlen(prefix);
    if (len >= MAX_WORD_LEN) {
        return NULL;
    }

    STireNode** node = (STireNode** )&tire->root.d;
    for (int i = 0; i < len; i++) {
        m = prefix[i] - FIRST_ASCII;
        if (m < 0 || m >= CHAR_CNT) {
            return NULL;
        }

        // match
        c = node[m];
        if (c == NULL) {
            // arrive end
            break;
        }

        // previous items already matched
        if (i == len - 1) {
            // malloc match if not pass by param match
            if (root == NULL) {                        
                root = (SMatch* )tmalloc(sizeof(SMatch));
                memset(root, 0, sizeof(SMatch));
                strcpy(root->pre, prefix);
            }

            // prefix is match to end char
            enumAllWords((STireNode** )&c->d, prefix, root);
        } else {
            // move to next node continue match
            node = (STireNode** )&c->d;
        }
    }

    // return 
    return root;
}

// get all items from tires tree
SMatch* enumAll(STire* tire) {
    char pre[2] ={0, 0};
    STireNode* c;

    SMatch* match = (SMatch* )tmalloc(sizeof(SMatch));
    memset(match, 0, sizeof(SMatch));
    
    // enum first layer
    for (int i = 0; i < CHAR_CNT; i++) {
        pre[0] = FIRST_ASCII + i;
        
        // each node
        c = tire->root.d[i];
        if (c == NULL) {
            // this branch no data
            continue;
        }

        // this branch have data
        matchPrefix(tire, pre, match);    
    }

    // return if need
    if (match->count == 0) {
        freeMatch(match);
        match = NULL;
    }
    return match;
}

// free match result
void freeMatchNode(SMatchNode* node) {
    // first free next
    if (node->next) {
        freeMatchNode(node->next);
    }

    // second free self
    tfree(node);
}

// free match result
void freeMatch(SMatch* match) {
    // first free next
    if (match->head) {
        freeMatchNode(match->head);
    }

    // second free self
    free(match);
}
