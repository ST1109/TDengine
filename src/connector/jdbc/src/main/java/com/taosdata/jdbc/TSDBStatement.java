/***************************************************************************
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
 *****************************************************************************/
package com.taosdata.jdbc;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.concurrent.ArrayBlockingQueue;

public class TSDBStatement extends AbstractStatement {
    /**
     * Status of current statement
     */
    private boolean isClosed;
    private TSDBConnection connection;
    private TSDBResultSet resultSet;
    ArrayBlockingQueue<AsyncResultPtr> queue = new ArrayBlockingQueue<>(1);

    TSDBStatement(TSDBConnection connection) {
        this.connection = connection;
        connection.registerStatement(this);
    }

    public ResultSet executeQuery(String sql) throws SQLException {
        synchronized (this) {
            if (isClosed()) {
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_STATEMENT_CLOSED);
            }
            if (this.resultSet != null && !this.resultSet.isClosed())
                this.resultSet.close();
            //TODO:
            // this is an unreasonable implementation, if the paratemer is a insert statement,
            // the JNI connector will execute the sql at first and return a pointer: pSql,
            // we use this pSql and invoke the isUpdateQuery(long pSql) method to decide .
            // but the insert sql is already executed in database.
            //execute query
            this.connection.getConnector().executeQuery(sql, this);
            AsyncResultPtr resultPtr;
            try {
                resultPtr = queue.take();
            } catch (InterruptedException e) {
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNKNOWN, "error occur while taking feedback message from executeUpdate");
            }
            if (resultPtr.getCode() < 0) {
                this.connection.getConnector().freeResultSet(resultPtr.getPtr());
                String msg = this.connection.getConnector().getErrMsg(resultPtr.getPtr());
                this.connection.getConnector().freeResultSet(resultPtr.getPtr());
                throw TSDBError.createSQLException(resultPtr.getCode(), msg);
            }
            // if pSql is create/insert/update/delete/alter SQL
            if (this.connection.getConnector().isUpdateQuery(resultPtr.getPtr())) {
                this.connection.getConnector().freeResultSet(resultPtr.getPtr());
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_INVALID_WITH_EXECUTEQUERY);
            }
            int timestampPrecision = this.connection.getConnector().getResultTimePrecision(resultPtr.getPtr());
            resultSet = new TSDBResultSet(this, this.connection.getConnector(), resultPtr.getPtr(), timestampPrecision);
            return resultSet;

        }
    }

    public int executeUpdate(String sql) throws SQLException {
        synchronized (this) {
            if (isClosed())
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_STATEMENT_CLOSED);
            if (this.resultSet != null && !this.resultSet.isClosed())
                this.resultSet.close();

            this.connection.getConnector().executeQuery(sql, this);
            AsyncResultPtr resultPtr;
            try {
                resultPtr = queue.take();
            } catch (InterruptedException e) {
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNKNOWN, "error occur while taking feedback message from executeUpdate");
            }

            if (resultPtr.getCode() < 0) {
                affectedRows = -1;
                String msg = this.connection.getConnector().getErrMsg(resultPtr.getPtr());
                this.connection.getConnector().freeResultSet(resultPtr.getPtr());
                throw TSDBError.createSQLException(resultPtr.getCode(), msg);
            }
            // if pSql is create/insert/update/delete/alter SQL
            if (!this.connection.getConnector().isUpdateQuery(resultPtr.getPtr())) {
                this.connection.getConnector().freeResultSet(resultPtr.getPtr());
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_INVALID_WITH_EXECUTEQUERY);
            }
            //TODO confirm the meaning of resultPtr.code
            int affectedRows = this.connection.getConnector().getAffectedRows(resultPtr.getPtr());
            this.connection.getConnector().freeResultSet(resultPtr.getPtr());
            return affectedRows;
        }
    }

    public void close() throws SQLException {
        if (isClosed)
            return;
        connection.unregisterStatement(this);
        if (this.resultSet != null && !this.resultSet.isClosed())
            this.resultSet.close();
        isClosed = true;
    }

    public boolean execute(String sql) throws SQLException {
        synchronized (this) {
            // check if closed
            if (isClosed()) {
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_STATEMENT_CLOSED);
            }
            if (this.resultSet != null && !this.resultSet.isClosed())
                this.resultSet.close();

            // execute query
            this.connection.getConnector().executeQuery(sql, this);
            AsyncResultPtr resultPtr;
            try {
                resultPtr = queue.take();
            } catch (InterruptedException e) {
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNKNOWN, "error occur while taking feedback message from execute");
            }
            if (resultPtr.getCode() < 0) {
                affectedRows = -1;
                String msg = this.connection.getConnector().getErrMsg(resultPtr.getPtr());
                this.connection.getConnector().freeResultSet(resultPtr.getPtr());
                throw TSDBError.createSQLException(resultPtr.getCode(), msg);
            }

            if (this.connection.getConnector().isUpdateQuery(resultPtr.getPtr())) {
                this.affectedRows = this.connection.getConnector().getAffectedRows(resultPtr.getPtr());
                this.connection.getConnector().freeResultSet(resultPtr.getPtr());
                return false;
            }

            int timestampPrecision = this.connection.getConnector().getResultTimePrecision(resultPtr.getPtr());
            this.resultSet = new TSDBResultSet(this, this.connection.getConnector(), resultPtr.getPtr(), timestampPrecision);
            return true;
        }
    }

    public ResultSet getResultSet() throws SQLException {
        if (isClosed()) {
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_STATEMENT_CLOSED);
        }

        return this.resultSet;
    }

    public int getUpdateCount() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_STATEMENT_CLOSED);
        return this.affectedRows;
    }

    public Connection getConnection() throws SQLException {
        if (isClosed()) {
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_STATEMENT_CLOSED);
        }

        if (this.connection.getConnector() == null) {
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_JNI_CONNECTION_NULL);
        }

        return this.connection;
    }

    public void setConnection(TSDBConnection connection) {
        this.connection = connection;
    }

    public boolean isClosed() throws SQLException {
        return isClosed;
    }

    public void queryFp(long ptr, int code) {
        AsyncResultPtr resultPtr = new AsyncResultPtr(ptr, code);
        queue.add(resultPtr);
    }
}
