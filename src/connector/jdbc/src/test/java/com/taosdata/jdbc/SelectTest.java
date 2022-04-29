package com.taosdata.jdbc;

import org.junit.BeforeClass;
import org.junit.Test;

import java.sql.*;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;

public class SelectTest {
    private static final String host = "localhost";
    private static final int port = 6030;
    private static Connection connectionRow;
    private static final String databaseName = "driver";

    public static void testInsert() throws SQLException {
        Statement statement = connectionRow.createStatement();
        statement.executeUpdate("drop database " + databaseName);
        statement.executeUpdate("create database " + databaseName);
        statement.execute("create table if not exists " + databaseName + ".alltype_query(ts timestamp, c1 bool,c2 tinyint, c3 smallint, c4 int, c5 bigint, c6 tinyint unsigned, c7 smallint unsigned, c8 int unsigned, c9 bigint unsigned, c10 float, c11 double, c12 binary(20), c13 nchar(20) )");
        long cur = System.currentTimeMillis();
        List<String> timeList = new ArrayList<>();
        for (long i = 0L; i < 3000; i++) {
            long t = cur + i;
            timeList.add("insert into " + databaseName + ".alltype_query values(" + t + ",1,1,1,1,1,1,1,1,1,1,1,'test_binary','test_nchar')");
        }
        for (int i = 0; i < 3000; i++) {
            statement.execute(timeList.get(i));
        }
        statement.close();
    }

    @Test
    public void testSelect() throws SQLException {
        for (int i = 0; i < 1; i++) {
            System.out.println("---- " + i + " ---");
            Statement statement = connectionRow.createStatement();
            select(statement);
        }
    }

    private void select(Statement statement) throws SQLException {
        long start = System.nanoTime();
        long queryTime = 0;
        long nextTime = 0;
        long getTime = 0;

        for (int i = 0; i < 1000; i++) {
            long e = System.nanoTime();
            ResultSet resultSet = statement.executeQuery("select * from " + databaseName + ".alltype_query limit 3000");
            queryTime += System.nanoTime() - e;
            long a = System.nanoTime();
            long tmp = 0;
            while (resultSet.next()) {
                long begin = System.nanoTime();
                resultSet.getBoolean(2);
                resultSet.getInt(3);
                resultSet.getInt(4);
                resultSet.getInt(5);
                resultSet.getLong(6);
                resultSet.getInt(7);
                resultSet.getInt(8);
                resultSet.getLong(9);
                resultSet.getLong(10);
                resultSet.getFloat(11);
                resultSet.getDouble(12);
                resultSet.getString(13);
                resultSet.getString(14);
                tmp += System.nanoTime() - begin;
            }
            nextTime += System.nanoTime() - a - tmp;
            getTime += tmp;
        }
        long d = System.nanoTime() - start;
        System.out.println("每次查询时间: " + d / 1000);
        System.out.println("query指针耗时" + ": " + queryTime / 1000);
        System.out.println("next数据指针耗时" + ": " + nextTime / 1000);
        System.out.println("解析数据耗时" + ": " + getTime / 1000);
        statement.close();
    }

    @BeforeClass
    public static void beforeClass() throws SQLException {
        String url = "jdbc:TAOS://" + host + ":" + port + "/?user=root&password=taosdata";
//        String url = "jdbc:TAOS-RS://" + host + ":6041/?user=root&password=taosdata";
        Properties properties = new Properties();
        properties.setProperty(TSDBDriver.PROPERTY_KEY_BATCH_LOAD, "true");
        connectionRow = DriverManager.getConnection(url, properties);
        testInsert();
    }
}