# -*- coding: utf-8 -*-

import copy
from util.log import *
from util.cases import *
from util.sql import *
from util.common import *
class TDTestCase:
    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)

    def run(self):
        tdSql.prepare()
        dbname = tdCom.getLongName(10, "letters")
        tdSql.execute(f'create database if not exists {dbname}')
        stbname = tdCom.getLongName(3, "letters")
        tdSql.execute(f'create stable if not exists {dbname}.{stbname} (col_ts timestamp, c1 tinyint, c2 smallint, c3 int, c4 bigint, c5 tinyint unsigned, c6 smallint unsigned, \
                c7 int unsigned, c8 bigint unsigned, c9 float, c10 double, c11 binary(16), c12 nchar(16), c13 bool) tags (tag_ts timestamp, t1 tinyint, t2 smallint, t3 int, \
                t4 bigint, t5 tinyint unsigned, t6 smallint unsigned, t7 int unsigned, t8 bigint unsigned, t9 float, t10 double, t13 bool)')
        base_sql = f'create table if not exists tb using {stbname} tags (now, 1, 2, 3, 4, 5, 6, 7, 8, 9.9, 10.1, True)'

        symbol_list = ['~', '`', '!', '@', '#', '$', '¥', '%', '^', '&', '*', '(', ')',
                        '-', '=', '{', '「', '[', ']', '}', '」', '、', '|', '\\', ':',
                        '\'', '\"', ',', '<', '《', '.', '>', '》', '/', '?']
        for insert_str in symbol_list:
            d_list = list(base_sql)
            for i in range(len(d_list)+1):
                d_list_new = copy.deepcopy(d_list)
                d_list_new.insert(i, insert_str)
                sql_new = ''.join(d_list_new)
                tdSql.error(sql_new)
        tdSql.execute(f'drop table if exists {dbname}.`{dbname}`')
        tdSql.execute(f'drop database if exists {dbname}')
    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
