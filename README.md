# FileCopy
For Linux/MacOS  
  
This tool allows to copy threaded with verbose output (Both optional).  
This tool aims to clone the same native usage of the build-in cp command, but makes it faster ( if threaded ) and clearer, easier to track the output ( if verbose )!  
Also a thing that bothered me was that if the destination directory does not exist, cp would tell you that, cpo will just create this on its own.  

## Disclaimer
Accessing the file is done with the rights of the executing user -> filerights are set to the executing user not inheriting the initial ones, means the file - if allowed to be accessed - is recreated with the rights of current user, follwing the convention of cp command.  

## Installation
Compile the .c file with your prefered compiler (`gcc cpo.c -o cpo`).
Probably move the file into your binary path, to make it globaly available. (`/usr/local/bin`)

## Usage
run `./cpo {OPTIONS} [SOURCE] [DESTINATION]`
You are allowed to provide more than one source (max. 256)

## Parameters
- `-r` for recursive copy
- `-v` for verbose output
- `-mt [INTEGER]` for threaded copy with [INTEGER] threads (Warning: User has full previleges to bombard his machine with threads)
