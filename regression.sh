#! /bin/sh

./a.out nikita ibk 1000 || exit 1
./a.out nikita dir 1 100 || exit 2
./a.out nikita dir 3 1000 || exit 3
./a.out jmacd build 3 1000 1000 || exit 4
./a.out vs || exit 5


