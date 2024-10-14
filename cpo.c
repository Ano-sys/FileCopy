#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>

#define MAX_SOURCES 256
#define MAX_PATH_LENGTH 1024

// Userset Flags : Standard Deactivated
uint8_t FLAG_MULTI_THREADING = 0;
uint8_t FLAG_VERBOSE = 0;
uint8_t FLAG_RECURSIVE = 0;

// Useraltered Flags : Standard Activated
uint8_t FLAG_CURRENT_FILE_ABSOLUTE = 1;
uint8_t FLAG_CURRENT_FILE_PERCENTAGE = 1;

// just let the user crash with threads
long GLOBAL_THREAD_COUNT = 1;
char *SOURCES[MAX_SOURCES];
char *DESTINATION;
long OPEN_THREADS_COUNTER = 0;
long FILE_COUNT = 0;
long CURRENT_FILE_INDEX = 0;

pthread_mutex_t LOCK_OPEN_THREADS_COUNTER;
pthread_mutex_t LOCK_FILE_COUNTS;
pthread_cond_t THREADS_COND;

typedef struct{
    char File[MAX_PATH_LENGTH];
    char Destination[MAX_PATH_LENGTH];
} Data;

void set_flag_by_char(char flag){
    flag = tolower((unsigned char)flag);
    switch(flag){
        case 'v':
            FLAG_VERBOSE = 1;
            break;
        case 'c':
            FLAG_CURRENT_FILE_ABSOLUTE = 0;
            break;
        case 'p':
            FLAG_CURRENT_FILE_PERCENTAGE = 0;
            break;
        case 'r':
            FLAG_RECURSIVE = 1;
            break;
    }
}

void set_flag_by_arg(char *arg){
    long length = strlen(arg);

    // skip only dash
    if(length == 1) return;

    // take apart the given argument
    for(long i = 1; i < length; i++){
        set_flag_by_char(arg[i]);
    }
}

void init_sources(){
    for(int i = 0; i < MAX_SOURCES; i++)
        SOURCES[i] = NULL;
}

void free_sources(){
    char **iterator = SOURCES;
    while(*iterator){
        free(*iterator++);
    }
}

void create_directory_structure(const char *path){
    char temp[MAX_PATH_LENGTH];
    char *p = NULL;
    size_t len;

    snprintf(temp, sizeof(temp), "%s", path);
    len = strlen(temp);
    if (temp[len - 1] == '/')
        temp[len - 1] = '\0';

    for(p = temp + 1; *p; p++){
        if(*p == '/'){
            *p = '\0';
            if (mkdir(temp, 0755) != 0 && errno != EEXIST){
                fprintf(stderr, "Error creating directory %s: %s\n", temp, strerror(errno));
            }
            *p = '/';
        }
    }
    if(mkdir(temp, 0755) != 0 && errno != EEXIST){
        fprintf(stderr, "Error creating directory %s: %s\n", temp, strerror(errno));
    }
}

void *copy_file(void *arg){
    Data *data = (Data*)arg;

    char dir[MAX_PATH_LENGTH];
    snprintf(dir, MAX_PATH_LENGTH, "%s", data->Destination);

    char *last_slash = strrchr(dir, '/');
    if(last_slash != NULL){
        *last_slash = '\0';
        // Verzeichnisstruktur erstellen
        create_directory_structure(dir);
    }

    // Quelldatei öffnen
    FILE *sfp = fopen(data->File, "rb");
    if(sfp == NULL){
        fprintf(stderr, "Could not open source file: %s, SKIPPED!\n", data->File);
        goto decrement_thread_counter;
    }

    // Zieldatei öffnen
    FILE *dfp = fopen(data->Destination, "wb");
    if(dfp == NULL){
        fprintf(stderr, "Could not open destination file: %s, SKIPPED!\n", data->Destination);
        fclose(sfp);
        goto decrement_thread_counter;
    }

    // Datei kopieren
    char buffer[8192];
    size_t bytes_read;
    while((bytes_read = fread(buffer, 1, sizeof(buffer), sfp)) > 0){
        size_t bytes_written = fwrite(buffer, 1, bytes_read, dfp);
        if (bytes_written < bytes_read) {
            fprintf(stderr, "Could not write to destination file %s\n", data->Destination);
            break;
        }
    }

    fclose(sfp);
    fclose(dfp);

    pthread_mutex_lock(&LOCK_FILE_COUNTS);
    CURRENT_FILE_INDEX++;
    if(FLAG_VERBOSE){
        printf("(%ld/%ld) Copied %s -> %s\n", CURRENT_FILE_INDEX, FILE_COUNT, data->File, data->Destination);
    }
    pthread_mutex_unlock(&LOCK_FILE_COUNTS);

    decrement_thread_counter:
    free(data);
    pthread_mutex_lock(&LOCK_OPEN_THREADS_COUNTER);
    OPEN_THREADS_COUNTER--;
    pthread_cond_signal(&THREADS_COND);
    pthread_mutex_unlock(&LOCK_OPEN_THREADS_COUNTER);

    return NULL;
}

long count_files(const char *directory){
    struct dirent *entry;
    struct stat file_stat;
    DIR *dir = opendir(directory);
    int file_counter = 0;

    if(dir == NULL){
        fprintf(stderr, "Could not open directory: %s\n", directory);
        return 0;
    }

    while((entry = readdir(dir)) != NULL){
        char fullPath[MAX_PATH_LENGTH];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", directory, entry->d_name);
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        if(lstat(fullPath, &file_stat) == 0){
            if(S_ISDIR(file_stat.st_mode)){
                file_counter += count_files(fullPath);
            }
            else if(S_ISREG(file_stat.st_mode)){
                file_counter++;
            }
        }
    }

    closedir(dir);
    return file_counter;
}

void handleSource(const char *directory_abs, const char *top_dir){
    struct dirent *entry;
    struct stat file_stat;
    DIR *dir = opendir(directory_abs);

    if(dir == NULL){
        fprintf(stderr, "Could not open directory: %s\n", directory_abs);
        return;
    }

    while((entry = readdir(dir)) != NULL){
        char fullPath[MAX_PATH_LENGTH];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", directory_abs, entry->d_name);
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        if (lstat(fullPath, &file_stat) == 0){
            if(S_ISDIR(file_stat.st_mode)){
                handleSource(fullPath, top_dir);
            }
            else if(S_ISREG(file_stat.st_mode)){
                pthread_mutex_lock(&LOCK_OPEN_THREADS_COUNTER);
                while(OPEN_THREADS_COUNTER >= GLOBAL_THREAD_COUNT){
                    pthread_cond_wait(&THREADS_COND, &LOCK_OPEN_THREADS_COUNTER);
                }
                OPEN_THREADS_COUNTER++;
                pthread_mutex_unlock(&LOCK_OPEN_THREADS_COUNTER);

                Data *data = malloc(sizeof(Data));
                if(data == NULL){
                    fprintf(stderr, "Memory allocation failed for Data structure\n");
                    break;
                }

                realpath(fullPath, data->File);

                char relativePath[MAX_PATH_LENGTH];
                if(strncmp(data->File, top_dir, strlen(top_dir)) == 0){
                    snprintf(relativePath, MAX_PATH_LENGTH, "%s", data->File + strlen(top_dir));
                    if (relativePath[0] == '/')
                        memmove(relativePath, relativePath+1, strlen(relativePath));
                }
                else{
                    snprintf(relativePath, MAX_PATH_LENGTH, "%s", data->File);
                }

                snprintf(data->Destination, MAX_PATH_LENGTH, "%s/%s", DESTINATION, relativePath);

                pthread_t copy_thread;
                if(pthread_create(&copy_thread, NULL, copy_file, (void*)data) != 0){
                    fprintf(stderr, "Error creating thread for %s\n", data->File);
                    free(data);
                    pthread_mutex_lock(&LOCK_OPEN_THREADS_COUNTER);
                    OPEN_THREADS_COUNTER--;
                    pthread_mutex_unlock(&LOCK_OPEN_THREADS_COUNTER);
                    continue;
                }

                pthread_detach(copy_thread);
            }
        }
    }
    closedir(dir);
}

void copy(){
    pthread_mutex_init(&LOCK_OPEN_THREADS_COUNTER, NULL);
    pthread_mutex_init(&LOCK_FILE_COUNTS, NULL);
    pthread_cond_init(&THREADS_COND, NULL);

    long file_count = 0;
    char **iterator = SOURCES;
    while(*iterator){
        if(FLAG_RECURSIVE){
            file_count += count_files(*iterator);
        }
        else{
            file_count++;
        }
        iterator++;
    }

    FILE_COUNT = file_count;

    iterator = SOURCES;
    while(*iterator){
        if(FLAG_RECURSIVE){
            handleSource(*iterator, *iterator);
        }
        else{
            Data *data = malloc(sizeof(Data));
            if(data == NULL){
                fprintf(stderr, "Memory allocation failed for Data structure\n");
                break;
            }

            realpath(*iterator, data->File);

            const char *filename = strrchr(data->File, '/');
            if(filename == NULL)
                filename = data->File;
            else
                filename++;

            snprintf(data->Destination, MAX_PATH_LENGTH, "%s/%s", DESTINATION, filename);

            pthread_mutex_lock(&LOCK_OPEN_THREADS_COUNTER);
            while(OPEN_THREADS_COUNTER >= GLOBAL_THREAD_COUNT){
                pthread_cond_wait(&THREADS_COND, &LOCK_OPEN_THREADS_COUNTER);
            }
            OPEN_THREADS_COUNTER++;
            pthread_mutex_unlock(&LOCK_OPEN_THREADS_COUNTER);

            pthread_t copy_thread;
            if(pthread_create(&copy_thread, NULL, copy_file, (void*)data) != 0){
                fprintf(stderr, "Error creating thread for %s\n", data->File);
                free(data);
                pthread_mutex_lock(&LOCK_OPEN_THREADS_COUNTER);
                OPEN_THREADS_COUNTER--;
                pthread_mutex_unlock(&LOCK_OPEN_THREADS_COUNTER);
                continue;
            }
            pthread_detach(copy_thread);
        }
        iterator++;
    }

    while(1){
        pthread_mutex_lock(&LOCK_OPEN_THREADS_COUNTER);
        if(OPEN_THREADS_COUNTER == 0){
            pthread_mutex_unlock(&LOCK_OPEN_THREADS_COUNTER);
            break;
        }
        pthread_mutex_unlock(&LOCK_OPEN_THREADS_COUNTER);
        usleep(100);
    }

    pthread_cond_destroy(&THREADS_COND);
}

int main(int argc, char **argv){
    if(argc < 3){
        printf("Usage: %s [Options] [SourceFiles...] [Destination]\n", *argv);
        exit(0);
    }

    init_sources();

    int source_counter = 0;
    int dest_index = argc - 1;
    for(int i = 1; i < argc - 1; i++){
        // handle parameters
        // handle multithreading
        if(strcmp(argv[i], "-mt") == 0){
            if(i == argc - 2){
                printf("Provide a Number for how many Threads should be created!\n");
                exit(-1);
            }
            long local_thread_count = atol(argv[++i]);
            if(local_thread_count == 0){
                printf("Provide a valid number for how many Threads should be created!\n");
                exit(-1);
            }
            if(local_thread_count < 0){
                local_thread_count = -local_thread_count;
            }
            GLOBAL_THREAD_COUNT = local_thread_count;
        }
        // handle other parameters
        else if(argv[i][0] == '-'){
            set_flag_by_arg(argv[i]);
        }
        else{
            char abs_path[PATH_MAX];
            if (realpath(argv[i], abs_path) == NULL) {
                perror("Error resolving absolute path");
                exit(-1);
            }
            SOURCES[source_counter] = strdup(abs_path);
            if (SOURCES[source_counter] == NULL) {
                    perror("Could not allocate Memory!");
                    exit(-2);
            }
            source_counter++;
        }
    }

    char abs_dest_path[PATH_MAX];
    if (realpath(argv[dest_index], abs_dest_path) == NULL) {
        strncpy(abs_dest_path, argv[dest_index], PATH_MAX);
    }
    DESTINATION = strdup(abs_dest_path);
    if (DESTINATION == NULL) {
            perror("Could not allocate Memory!");
            exit(-1);
    }

    // start the copy program with all things set up
    copy();

    // wait on threads and finish program

    free_sources();
    free(DESTINATION);
}
