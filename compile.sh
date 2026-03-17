#!/bin/bash

DEVICE="/dev/ttyACM0"
DEVICE_PARAM=""
if [ -e "$DEVICE" ]; then
    DEVICE_PARAM="--device=$DEVICE"
fi

docker run -it --rm \
    $DEVICE_PARAM \
    --user $(id -u):$(id -g) \
    --group-add dialout \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/group:/etc/group:ro \
    -v "$PWD":/project \
    -v "$HOME/.espressif_ccache":/.ccache \
    -e CCACHE_DIR=/.ccache \
    -w /project \
    --env HISTFILE=/project/.bash_history \
    espressif/idf:latest \
    idf.py build

