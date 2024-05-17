#!/bin/bash

CURR_DIR=$(cd "$(dirname "$0")";pwd)

find ${CURR_DIR} -name "debug.*" | xargs rm -rf;
find ${CURR_DIR} -name "release.*" | xargs rm -rf;
