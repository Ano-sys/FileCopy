# FileCopy
This tool allows to copy threaded with verbose output. 

## Installation
compile the .c file with your prefered compiler (gcc cpo.c -o cpo).  
Probably move the file into your binary path, to make it globaly available.  

## Usage
run ./cpo [SOURCE] [DESTINATION]
You are allowed to provide more than one source (max. 256)

## Parameters
- '-r' for recurive copy
- '-v' for verbose output
- '-mt [INTEGER]' for threaded copy with [INTEGER] threads
