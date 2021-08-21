#!/bin/bash

loader_file_size=$(wc -c <build/bootloader/loader.bin)
let loader_sectors=($loader_file_size+513)/512
let desired_loader_bytes=$loader_sectors*512
let loader_pad_bytes=$desired_loader_bytes-$loader_file_size
dd if=/dev/zero of=build/bootloader/loader.bin bs=1 count=$loader_pad_bytes seek=$loader_file_size
