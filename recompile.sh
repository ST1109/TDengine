#!/bin/bash
set -e

cd /root/TDengine/debug
rm -rf /root/TDengine/debug/*
cmake .. -DBUILD_TOOLS=true -DSANITIZER=true -DBUILD_TYPE=Debug
#cmake .. -DBUILD_TOOLS=true -DBUILD_TYPE=Debug 
#cmake .. 
make -j24
make install

