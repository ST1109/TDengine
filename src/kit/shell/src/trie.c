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
#include "trie.h"

// ----------- interface -------------

// create prefix search tree
STrie* createTrie() {
     STrie* trie = malloc(sizeof(STrie));
     memset(trie, 0, sizeof(STrie));
     return trie;
}

// free trie node
void freeTrieNode(STrieNode* node) {
    // nest free sub node on array d 
    for(int i = 0; i < CHAR_CNT; i++) {
        freeTrieNode(node->d[i]);
    }

    // free self
    tfree(node);
}

// destroy prefix search tree
void freeTrie(STrie* trie) {
    // free nodes
    for(int i = 0; i < CHAR_CNT; i++) {
        freeTrieNode(trie->root.d[i]);
    }

    // free trie
    tfree(trie);
}

// insert a new word 
bool insertWord(STrie* trie, char* word) {
    int m  = 0;
    STrieNode* c = 0;
    int len = strlen(word);
    if(len >= MAX_WORD_LEN) {
        return false;
    }

    STrieNode** node = (STrieNode** )&trie->root.d;
    for(int i = 0; i < len; i++) {
        m = word[i] - FIRST_ASCII;
        if(m < 0 || m >= CHAR_CNT) {
            return false;
        }

        c = node[m];
        if (c == NULL) {
            // no set value
            if(i != len - 1) {
                // not end char, malloc new node
                STrieNode* p = (STrieNode* )tmalloc(sizeof(STrieNode));
                memset(p, 0, sizeof(STrieNode));
                node[m] = p;
            } else {
                // is end char, change NULL to PTR_END
                node[m] = PTR_END;
                break;
            }
        }else if (c == PTR_END) {
            // set end by previous word 
            if(i != len - 1) {
                // current word is large than previous, malloc new node
                STrieNode* p = (STrieNode* )tmalloc(sizeof(STrieNode));
                memset(p, 0, sizeof(STrieNode));
                node[m] = p;
            } else {
                // current word same with previous, do nothing                
            }
        } else {
            // c is not null, do nothing
        }

        // move to next node
        node = (STrieNode** )&c->d;
    }

    // add count
    trie->count += 1;
    return true;
}

void addwordToMatch(SMatch* match, char* word){
    // malloc new
    SMatchNode* node = (SMatchNode* )tmalloc(sizeof(SMatchNode));
    strcpy(node->word, word);

    // append to match
    if(match->head  == NULL) {
        match->head = match->tail = node;
    } else {
        match->tail->next = node;
        match->tail = node;
    }
    match->count += 1;
}

// enum all words from node
void enumAllWords(STrieNode** node,  char* prefix, SMatch* match) {
    STrieNode * c;
    char word[CHAR_CNT];
    int len = strlen(prefix);
    for(int i = 0; i < CHAR_CNT; i++) {
        c = node[i];
        
        // combine word string
        memset(word, 0, sizeof(word));
        strcpy(word, prefix);
        word[len] =  FIRST_ASCII + i; // append current char

        if(c == PTR_END) {
            // str end, append to match list
            addwordToMatch(match, word); 
        } else if(c != NULL) {
            // have sub nodes, continue enum
            enumAllWords((STrieNode** )&c->d, word, match);
        }
    }
}

// match prefix words
SMatch* matchPrefix(STrie* trie, char* prefix) {
    SMatch* root = NULL;
    int m  = 0;
    STrieNode* c = 0;
    int len = strlen(prefix);
    if(len >= MAX_WORD_LEN) {
        return NULL;
    }

    STrieNode** node = &trie->root.d;
    for(int i = 0; i < len; i++) {
        m = prefix[i] - FIRST_ASCII;
        if(m < 0 || m >= CHAR_CNT) {
            return NULL;
        }

        // match
        c = node[m];
        if (c == NULL) {
            // no match 
            break;
        }else if (c == PTR_END) {
            // match 
            if(i == len - 1) {
                // match whole word
            } else {
                // match part, but prefix behind can't matched, so no match
                break;
            }
        } else {
            // previous matched, need check prefix is end
            if(i == len - 1) {
                // malloc match
                root = (SMatch* )tmalloc(sizeof(SMatch));
                memset(root, 0, sizeof(SMatch));

                // prefix is match to end char
                enumAllWords((STrieNode** )&c->d, prefix, root);
            } else {
                // move to next node continue match
                node = (STrieNode** )&c->d;
            }         
        }
    }

    // return 
    return root;
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
    tfree(match);
}
