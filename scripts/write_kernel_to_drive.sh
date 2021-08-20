#!/bin/sh

dd if=/dev/zero of=build/kernel/hdd.bin bs=1024 count=102400                                     
kernel_size=$(wc -c <build/kernel/kernel.o)                                                     
dd if=build/kernel/kernel.o of=build/kernel/hdd.bin count=$kernel_size conv=notrunc
