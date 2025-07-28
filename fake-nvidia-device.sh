#!/bin/bash

MAJOR=195
NUM_GPUS=4
mknod /dev/nvidiactl c $MAJOR 255
mknod /dev/nvidia-modeset c $MAJOR 254

for i in $(seq 0 $(($NUM_GPUS - 1))); do
  mknod /dev/nvidia$i c $MAJOR $i
done
chmod 666 /dev/nvidia*
