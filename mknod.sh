#!/bin/bash
mknod /dev/nvidiactl c 195 255
chmod 666 /dev/nvidiactl

for i in {0..3}; do
    mknod /dev/nvidia$i c 195 $i
    chmod 666 /dev/nvidia$i
done