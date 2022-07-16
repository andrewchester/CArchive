#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

// Based on NAME_MAX from limits.h
#define NAME_MAX 255 

int mkpath(const char*, mode_t);
void next_block(FILE*, char*, const char, const int);
void pack(const char*, FILE*, int);
int unpack(FILE*, int);

/**
 * Like mkdir, but creates parent paths as well
 *
 * @return 0, or -1 on error, with errno set
 * @see mkdir(2)
 */
int mkpath(const char *pathname, mode_t mode) {
    char *tmp = malloc(strlen(pathname) + 1);
    strcpy(tmp, pathname);
    for (char *p = tmp; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            struct stat st;
            if (stat(tmp, &st))  {
                if (mkdir(tmp, mode)) {
                    free(tmp);
                    return -1;
                }
            } else if (!S_ISDIR(st.st_mode)) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    free(tmp);
    return 0;
}

/**
 * @brief Reads input file until the next delimiter, writes bytes to output
 * 
 * @param fp          pointer to input file
 * @param output      array to output to
 * @param delim       the delimiter to stop at
 * @param max_reads   limit on how many characters to read
 */
void next_block (FILE* fp, char* output, const char delim, const int max_reads) {
    char buff;
    unsigned short i = 0;
    // place all bytes into fn until we reach a delimiter
    while (i < max_reads && fread(&buff, sizeof(char), 1, fp) && buff != delim)
        output[i++] = buff;
    output[i] = '\0';
}

/** 
 * Packs a single file or directory recursively
 *
 * @param fn The filename to pack
 * @param outfp The file to write encoded output to
 */
void pack(const char* fn, FILE *outfp, int depth) {
    struct stat st;
    stat(fn, &st);

    if (S_ISDIR(st.st_mode)) { // packs a directory
        for (int i = 0; i < depth; i++)
            fprintf(stdout, "  ");
        fprintf(stdout, "%s/\n", fn);

        DIR* indir = opendir(fn); // open the directory
        if (!indir) {
            fprintf(stderr, "Failed: %s is an incorrect directory.\n", fn);
            exit(1);
        }

        fprintf(outfp, "%ld:%s/", strlen(fn)+1, fn);

        struct dirent *f;
        chdir(fn); // change the working directory
        while ((f = readdir(indir)) != NULL) { // recursively read each file in directory
            if(!strcmp(f->d_name, ".") || !strcmp(f->d_name, ".."))
                continue;
            pack(f->d_name, outfp, depth + 1);
        }
        fprintf(outfp, "0:"); // add end of directory marker
        chdir(".."); // leave directory
        closedir(indir);
    } else if (S_ISREG(st.st_mode)) { // pack a single file
        for (int i = 0; i < depth; i++)
            fprintf(stdout, "  ");
        fprintf(stdout, "%s\n", fn);
        
        FILE* infp = fopen(fn, "r"); // open the file
        if (!infp) {
            fprintf(stderr, "Failed: %s is an incorrect filepath.\n", fn);
            exit(1);
        }

        // print name length: name filesize:
        fprintf(outfp, "%ld:%s%ld:", strlen(fn), fn, st.st_size);

        // write file contents to archive
        unsigned char buff;
        for (;;) {
            if (fread(&buff, sizeof(unsigned char), 1, infp))
                fwrite(&buff, sizeof(char), 1, outfp);
            else
                break;
        }
        fclose(infp);
    } else {
        fprintf(stderr, "Skipping non-regular file `%s'.\n", fn);
    }
}

/**
 * Unpacks an entire archive
 *
 * @param fp     The archive to unpack
 * @param depth  Keeps track of depth for printing file structure
 */
int unpack(FILE *fp, int depth) {
    // NAME_MAX identifies the longest filename possible
    char fn[NAME_MAX];
    char buff[NAME_MAX];
    int fn_length = 0; // length of file name
    int file_length = 0; // length of file itself
    
    fprintf(stdout, "Contents of Extracted Archive:\n");
    while(1) {
        next_block(fp, buff, ':', NAME_MAX); // extracts length of name
        fn_length = atoi(buff);
        next_block(fp, fn, ':', fn_length); // extracts file name

        if (fn_length == 0) { // end of directory
            chdir("..");
            depth--;
        } else if (fn[strlen(fn)-1] == '/') { // create directory
            if (mkpath(fn, 0700)) 
                err(errno, "mkpath()");
            for (int i = 0; i < depth; i++)
                fprintf(stdout, "  ");
            fprintf(stdout, "%s\n", fn);
            chdir(fn);
            depth++;
        } else {
            for (int i = 0; i < depth; i++)
                fprintf(stdout, "  ");
            fprintf(stdout, "%s\n", fn);

            // get length of file
            next_block(fp, buff, ':', NAME_MAX);
            file_length = atoi(buff);

            // create and write from archive to file
            FILE* newfile = fopen(fn, "w");
            char buffer;
            for (int i = 0; i < file_length; i++) {
                fread(&buffer, sizeof(char), 1, fp);
                fwrite(&buffer, sizeof(char), 1, newfile);
            }
            fclose(newfile);
        }

        // exit if at end of file
        int next = getc(fp);
        if (next == EOF)
            break;
        else
            ungetc(next, fp);
    };
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s FILE... OUTFILE\n"
                        "       %s INFILE\n", argv[0], argv[0]);
        exit(1);
    }

    char *fn = argv[argc-1]; // output file
    if (argc > 2) { /* Packing files */
        FILE* fp = fopen(fn, "w");
        if (!fp) {
            fprintf(stderr, "Failed: %s is not a valid filepath.\n", fn);
            exit(1);
        }

        fprintf(stdout, "Contents of Archive:\n");
        for (int argind = 1; argind < argc - 1; ++argind) {
            pack(argv[argind], fp, 1);
        }
        fclose(fp);
    } else { /* Unpacking an archive file */
        FILE *fp = fopen(fn, "r");
        if (!fp) {
            fprintf(stderr, "Failed: %s is not a valid filepath.\n", fn);
            exit(1);
        }
        unpack(fp, 1);
        fclose(fp);
    }
}