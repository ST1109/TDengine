package com.taosdata.jdbc;

public class AsyncResultPtr {
    private long ptr;
    private int code;

    public AsyncResultPtr(long ptr, int code) {
        this.ptr = ptr;
        this.code = code;
    }

    public long getPtr() {
        return ptr;
    }

    public int getCode() {
        return code;
    }
}
