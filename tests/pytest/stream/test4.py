# -*- coding: utf-8 -*-

from util.log import *
from util.cases import *
from util.sql import *
class TDTestCase:
    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)

    def wait_stream_res(self, sql, expected_res):
        tdSql.query(sql)
        latency = 0

        while tdSql.queryRows != expected_res:
            tdSql.query(sql)
            if latency < 2:
                latency += 0.01
                time.sleep(0.01)
            else:
                tdSql.checkEqual(tdSql.queryRows, expected_res)

    def check_query_data(self, sql1, sql2):
        res1 = tdSql.query(sql1, True)
        res2 = tdSql.query(sql2, True)
        tdSql.checkEqual(res1, res2)

    def check_stream(self, sql1, sql2, expected_count):
        self.wait_stream_res(sql1, expected_count)
        self.check_query_data(sql1, sql2)

    def run(self):
        tdSql.execute('drop database if exists mcrwymeqhz')
        tdSql.execute('create database if not exists mcrwymeqhz vgroups 1')
        tdSql.execute('use mcrwymeqhz')
        tdSql.execute('create table if not exists scalar_stb (ts timestamp, c1 int, c2 double, c3 binary(20), c4 binary(20), c5 nchar(20)) tags (t1 int);')
        tdSql.execute('create table scalar_ct1 using scalar_stb tags(10);')
        tdSql.execute('create table if not exists scalar_tb (ts timestamp, c1 int, c2 double, c3 binary(20), c4 binary(20), c5 nchar(20));')
        math_function_list = ["abs", "acos", "asin", "atan", "ceil", "cos", "floor", "log", "pow", "round", "sin", "sqrt", "tan"]
        # string_function_list = ["lower", "ltrim", "rtrim", "substr", "upper"]
        string_function_list = ["char_length", "concat", "concat_ws", "length", "lower", "ltrim", "rtrim", "substr", "upper"]
        for math_function in math_function_list:
            if math_function in ["log", "pow"]:
                tdSql.execute(f'create stream stb_{math_function}_stream into output_{math_function}_stb as select ts, {math_function}(c1, 2), {math_function}(c2, 2), c3 from scalar_stb;')
                tdSql.execute(f'create stream ctb_{math_function}_stream into output_{math_function}_ctb as select ts, {math_function}(c1, 2), {math_function}(c2, 2), c3 from scalar_ct1;')
                tdSql.execute(f'create stream tb_{math_function}_stream into output_{math_function}_tb as select ts, {math_function}(c1, 2), {math_function}(c2, 2), c3 from scalar_tb;')
            else:
                tdSql.execute(f'create stream stb_{math_function}_stream into output_{math_function}_stb as select ts, {math_function}(c1), {math_function}(c2), c3 from scalar_stb;')
                tdSql.execute(f'create stream ctb_{math_function}_stream into output_{math_function}_ctb as select ts, {math_function}(c1), {math_function}(c2), c3 from scalar_ct1;')
                tdSql.execute(f'create stream tb_{math_function}_stream into output_{math_function}_tb as select ts, {math_function}(c1), {math_function}(c2), c3 from scalar_tb;')
        for string_function in string_function_list:
            if string_function == "concat":
                tdSql.execute(f'create stream stb_{string_function}_stream into output_{string_function}_stb as select ts, {string_function}(c3, c4), {string_function}(c3, c5), {string_function}(c4, c5), {string_function}(c3, c4, c5) from scalar_stb;')
                tdSql.execute(f'create stream ctb_{string_function}_stream into output_{string_function}_ctb as select ts, {string_function}(c3, c4), {string_function}(c3, c5), {string_function}(c4, c5), {string_function}(c3, c4, c5) from scalar_ct1;')
                tdSql.execute(f'create stream tb_{string_function}_stream into output_{string_function}_tb as select ts, {string_function}(c3, c4), {string_function}(c3, c5), {string_function}(c4, c5), {string_function}(c3, c4, c5) from scalar_tb;')
            elif string_function == "concat_ws":
                tdSql.execute(f'create stream stb_{string_function}_stream into output_{string_function}_stb as select ts, {string_function}("aND", c3, c4), {string_function}("and", c3, c5), {string_function}("And", c4, c5), {string_function}("AND", c3, c4, c5) from scalar_stb;')
                tdSql.execute(f'create stream ctb_{string_function}_stream into output_{string_function}_ctb as select ts, {string_function}("aND", c3, c4), {string_function}("and", c3, c5), {string_function}("And", c4, c5), {string_function}("AND", c3, c4, c5) from scalar_ct1;')
                tdSql.execute(f'create stream tb_{string_function}_stream into output_{string_function}_tb as select ts, {string_function}("aND", c3, c4), {string_function}("and", c3, c5), {string_function}("And", c4, c5), {string_function}("AND", c3, c4, c5) from scalar_tb;')
            elif string_function == "substr":
                tdSql.execute(f'create stream stb_{string_function}_stream into output_{string_function}_stb as select ts, {string_function}(c3, 2), {string_function}(c3, 2, 2), {string_function}(c4, 5, 1), {string_function}(c5, 3, 4) from scalar_stb;')
                tdSql.execute(f'create stream ctb_{string_function}_stream into output_{string_function}_ctb as select ts, {string_function}(c3, 2), {string_function}(c3, 2, 2), {string_function}(c4, 5, 1), {string_function}(c5, 3, 4) from scalar_ct1;')
                tdSql.execute(f'create stream tb_{string_function}_stream into output_{string_function}_tb as select ts, {string_function}(c3, 2), {string_function}(c3, 2, 2), {string_function}(c4, 5, 1), {string_function}(c5, 3, 4) from scalar_tb;')
            else:
                tdSql.execute(f'create stream stb_{string_function}_stream into output_{string_function}_stb as select ts, {string_function}(c3), {string_function}(c4), {string_function}(c5) from scalar_stb;')
                tdSql.execute(f'create stream ctb_{string_function}_stream into output_{string_function}_ctb as select ts, {string_function}(c3), {string_function}(c4), {string_function}(c5) from scalar_ct1;')
                tdSql.execute(f'create stream tb_{string_function}_stream into output_{string_function}_tb as select ts, {string_function}(c3), {string_function}(c4), {string_function}(c5) from scalar_tb;')
        date_time = 1653547828591
       
        count = 1
        step_count = 1
        for i in range(1, 20):
            if i % 2 == 0:
                step_count += i
                for j in range(count, step_count):
                    tdSql.execute(f'insert into scalar_ct1 values ({date_time}+{j}s, 100, -100.1, "hebei", Null, "Bigdata");')
                    tdSql.execute(f'insert into scalar_tb values ({date_time}+{j}s, 100, -100.1, "heBei", Null, "Bigdata");')
                count += i
            else:
                step_count += 1
                for i in range(2):
                    tdSql.execute(f'insert into scalar_ct1 values ({date_time}+{count}s, -50, 50.1, "beiJing", "TDengine", "taos");')
                    tdSql.execute(f'insert into scalar_tb values ({date_time}+{count}s, -50, 50.1, "beiJing", "TDengine", "taos");')
                count += 1
            for math_function in math_function_list:
                if math_function == "log" or math_function == "pow":
                    self.check_stream(f'select `{math_function}(c1, 2)`, `{math_function}(c2, 2)` from output_{math_function}_stb;', f'select {math_function}(c1, 2), {math_function}(c2, 2) from scalar_stb;', count-1)
                    self.check_stream(f'select `{math_function}(c1, 2)`, `{math_function}(c2, 2)` from output_{math_function}_ctb;', f'select {math_function}(c1, 2), {math_function}(c2, 2) from scalar_ct1;', count-1)
                    self.check_stream(f'select `{math_function}(c1, 2)`, `{math_function}(c2, 2)` from output_{math_function}_tb;', f'select {math_function}(c1, 2), {math_function}(c2, 2) from scalar_tb;', count-1)
                else:
                    self.check_stream(f'select `{math_function}(c1)`, `{math_function}(c2)` from output_{math_function}_stb;', f'select {math_function}(c1), {math_function}(c2) from scalar_stb;', count-1)
                    self.check_stream(f'select `{math_function}(c1)`, `{math_function}(c2)` from output_{math_function}_ctb;', f'select {math_function}(c1), {math_function}(c2) from scalar_ct1;', count-1)
                    self.check_stream(f'select `{math_function}(c1)`, `{math_function}(c2)` from output_{math_function}_tb;', f'select {math_function}(c1), {math_function}(c2) from scalar_tb;', count-1)
                for string_function in string_function_list:
                    if string_function == "concat":
                        self.check_stream(f'select `{string_function}(c3, c4)`, `{string_function}(c3, c5)`, `{string_function}(c4, c5)`, `{string_function}(c3, c4, c5)` from output_{string_function}_stb;', f'select {string_function}(c3, c4), {string_function}(c3, c5), {string_function}(c4, c5), {string_function}(c3, c4, c5) from scalar_stb;', count-1)
                        self.check_stream(f'select `{string_function}(c3, c4)`, `{string_function}(c3, c5)`, `{string_function}(c4, c5)`, `{string_function}(c3, c4, c5)` from output_{string_function}_ctb;', f'select {string_function}(c3, c4), {string_function}(c3, c5), {string_function}(c4, c5), {string_function}(c3, c4, c5) from scalar_ct1;', count-1)
                        self.check_stream(f'select `{string_function}(c3, c4)`, `{string_function}(c3, c5)`, `{string_function}(c4, c5)`, `{string_function}(c3, c4, c5)` from output_{string_function}_tb;', f'select {string_function}(c3, c4), {string_function}(c3, c5), {string_function}(c4, c5), {string_function}(c3, c4, c5) from scalar_tb;', count-1)
                    elif string_function == "concat_ws":
                        self.check_stream(f'select `{string_function}("aND", c3, c4)`, `{string_function}("and", c3, c5)`, `{string_function}("And", c4, c5)`, `{string_function}("AND", c3, c4, c5)` from output_{string_function}_stb;', f'select {string_function}("aND", c3, c4), {string_function}("and", c3, c5), {string_function}("And", c4, c5), {string_function}("AND", c3, c4, c5) from scalar_stb;', count-1)
                        self.check_stream(f'select `{string_function}("aND", c3, c4)`, `{string_function}("and", c3, c5)`, `{string_function}("And", c4, c5)`, `{string_function}("AND", c3, c4, c5)` from output_{string_function}_ctb;', f'select {string_function}("aND", c3, c4), {string_function}("and", c3, c5), {string_function}("And", c4, c5), {string_function}("AND", c3, c4, c5) from scalar_ct1;', count-1)
                        self.check_stream(f'select `{string_function}("aND", c3, c4)`, `{string_function}("and", c3, c5)`, `{string_function}("And", c4, c5)`, `{string_function}("AND", c3, c4, c5)` from output_{string_function}_tb;', f'select {string_function}("aND", c3, c4), {string_function}("and", c3, c5), {string_function}("And", c4, c5), {string_function}("AND", c3, c4, c5) from scalar_tb;', count-1)
                    elif string_function == "substr":
                        self.check_stream(f'select `{string_function}(c3, 2)`, `{string_function}(c3, 2, 2)`, `{string_function}(c4, 5, 1)`, `{string_function}(c5, 3, 4)` from output_{string_function}_stb;', f'select {string_function}(c3, 2), {string_function}(c3, 2, 2), {string_function}(c4, 5, 1), {string_function}(c5, 3, 4) from scalar_stb;', count-1)
                        self.check_stream(f'select `{string_function}(c3, 2)`, `{string_function}(c3, 2, 2)`, `{string_function}(c4, 5, 1)`, `{string_function}(c5, 3, 4)` from output_{string_function}_ctb;', f'select {string_function}(c3, 2), {string_function}(c3, 2, 2), {string_function}(c4, 5, 1), {string_function}(c5, 3, 4) from scalar_ct1;', count-1)
                        self.check_stream(f'select `{string_function}(c3, 2)`, `{string_function}(c3, 2, 2)`, `{string_function}(c4, 5, 1)`, `{string_function}(c5, 3, 4)` from output_{string_function}_tb;', f'select {string_function}(c3, 2), {string_function}(c3, 2, 2), {string_function}(c4, 5, 1), {string_function}(c5, 3, 4) from scalar_tb;', count-1)
                    else:
                        self.check_stream(f'select `{string_function}(c3)`, `{string_function}(c4)`, `{string_function}(c5)` from output_{string_function}_stb;', f'select {string_function}(c3), {string_function}(c4), {string_function}(c5) from scalar_stb;', count-1)
                        self.check_stream(f'select `{string_function}(c3)`, `{string_function}(c4)`, `{string_function}(c5)` from output_{string_function}_ctb;', f'select {string_function}(c3), {string_function}(c4), {string_function}(c5) from scalar_ct1;', count-1)
                        self.check_stream(f'select `{string_function}(c3)`, `{string_function}(c4)`, `{string_function}(c5)` from output_{string_function}_tb;', f'select {string_function}(c3), {string_function}(c4), {string_function}(c5) from scalar_tb;', count-1)

    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
