#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <regex.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

/*
 * This program works in the command line and takes input through command line 
 * arguments. The first argument after the program name is the flag argument
 * which represents different operations that the program can do. Any further 
 * arguments needed by the program are entered after the flag argument. The 
 * different available flags, the required arguments for each flag and the order
 * in which they must be entered can be viewed when the combination of arguments
 * are incorrect (e.g. when the program is run with no further command line 
 * arguments). The extra command line arguments that are required for each 
 * operation are validated depending on the type of input it corresponds to.
 * This includes general strings (checking lengths), file paths (checking 
 * lengths, and file) and numbers (checking lengths, numerical characters and 
 * range of value). After inputs are validated, the corresponding functions
 * linked to each operation are called. The function will attempt to carry out
 * the desired operation, but in the case an unfixable error is thrown, the 
 * program prints an error message and then exits (closing any files if they 
 * are open). 
 * 
 * Operations include: 
 * create_file, copy_file, del_file, show_file, show_line, del_line, append_line
 * ins_line, rep_line, search, regex_search, replace, count_lines, display_log
 * 
 * create-file - create a new file or if exists overwrite with user confirmation
 * copy-file - copy contents of source file to destination (if exists overwrite 
 *             with user confirmation)
 * del_file - delete file 
 * show_file - display contents of file with line numbers
 * show_line - display specified line in file
 * del_line - delete specified line from file
 * append_line - add specified line to end of file
 * ins_line - add specfied line to provided position (line num) in file
 * rep_line - replace specified line in file with string 
 * search - search for string in file
 * regex_search - search for regex matches in file
 * replace - replace instances of string in file with another string
 * count_lines - count number of lines in file (0 if empty)
 * display_log - displays the history of operations performed on file (if no 
 *               file provided, display for all files)
 * 
 * Some operations (truncate_log, del_line, ins_line, rep_line, replace) 
 * require a temporary intermediate file that is renamed to replace the 
 * original file. Some operations (copy_file, create_file) require confirmation
 * from the user when the file exists and will be overwritten. This input is 
 * taken from the standard input stream and is also validated.
 * 
 * Some operations (display_log, truncate_log, replace, search, regex_search) 
 * read files line by line using fgets() and therefore files used for these 
 * operations are verified for their max line length and whether they contain 
 * NULL characters before any edits are made. Other operations read files char 
 * by char and thereby can handle files with larger lines and null characters 
 * - (more versatile)
 * 
 * Operations that modify the file, log the operation that was carried with: a 
 * timestamp, the files involved, the inputs involved as well as the number of 
 * lines in the resulting file after the operation. This is logged to a global 
 * file. display_log allows for viewing of a global log or a log for a specific 
 * file. In order to prevent endless growth of log file, a limit is kept on how 
 * far back the log goes. 
 * 
 * Inputs are heavily validated and Errors are safely handled to ensure that 
 * there are little to no cases where the program will break unexpectedly 
 * without an error message.
 */

/* --- CONSTANTS --- */

/*
 * enum that defines constants that represent limits used for the length of 
 * specific inputs such as a file path or a general string and even more. These
 * limits are used through the program and therefore are defined as constants
 * LOGLEN - maximum length of log string to be written/read 
 * MAX - maximum length of general string inputs
 * MAXF - maximum length of file path or regex expression
 * CLOG_BUFFER - how far back the log file stores log statements from (minimum 10)
 */
enum {
    LOGLEN = 2560,
    MAX = 1024,
    MAXF = 256,
    CLOG_BUFFER = 200
};

// Regex Expression to check validty of new filepaths
static const char FNAME[] = "^((\\/)?[0-9a-zA-Z._-][0-9a-zA-Z._ -]*)+$";
// file path of log file
static const char LOGF[] = "editorback.log";
// file path of temporary file used in some edit operations
static const char TEMPF[] = "tempeditor.tmp";

/* --- MISC --- */

/*
 * Function: die()
 * -----------------------------
 * Prints error message corresponding to current value of errno and exits
 * 
 * s: string to be printed alongside errno error message
 */
void die(const char *s) {
    perror(s);
    exit(1);
}

/*
 * Function is_file()
 * -----------------------------
 * Checks if the file at the provided path is a regular file (if it exists)
 * 
 * path: string file path to file being checked
 * 
 * returns: non-zero if regular file, else returns 0
 */
int is_file(char *path) {
    struct stat sb;
    // Retrieve information regarding file (with error handling)
    if (stat(path, &sb) != 0) {
        die("stat");
    }

    // Return file is regular or not
    return S_ISREG(sb.st_mode);
}

/*
 * Function is_empty()
 * -----------------------------
 * Checks if the file at the provided path is empty (if it exists)
 * 
 * path: string file path to file being checked
 * 
 * returns: 1 if file is empty, else returns 0
 */
int is_empty(char *path) {
    struct stat sb;
    // Retrieve information regarding file (with error handling)
    if (stat(path, &sb) != 0) {
        die("stat");
    }

    // If file is empty returns 1
    if (sb.st_size <= 1) return 1;
    return 0;
}

/*
 * Function count_lines()
 * -----------------------------
 * Counts the number of lines from the position specified by the file pointer to
 * the end of the file. 
 * 
 * fptr: pointer to file being read
 * 
 * returns: number of lines in the file
 */
size_t count_lines(FILE **fptr) {
    char c;
    size_t lines = 0;
    int first = 1;

    // Read char from file until end of file reached
    do {
        c = getc(*fptr);
        // Handle empty files such that they have 0 lines
        if (first && c != EOF) {
            lines += 1;
            first = 0;
        }
        // If newline char increment line counter
        if (c == '\n') lines += 1;
    } while (c != EOF);

    return lines;
}

/*
 * Function verify_lines()
 * -----------------------------
 * Counts the number of lines from the position specified by the file pointer to
 * the end of the file. It also checks the length of the lines to ensure they
 * do not pass a specified limit and that there are no NULL characters in the 
 * file (validates the file for safe usage with fgets)
 * 
 * fptr: pointer to file being read
 * max_val: maximum length of the lines allowed in the file
 * 
 * returns: number of lines if file passes checks, else -1
 */
ssize_t verify_lines(FILE **fptr, int max_val) {
    char c;
    size_t lines = 0;
    size_t linelen = 0;
    int first = 1;

    // Read char from file until end of file reached
    do {
        c = getc(*fptr);
        if (c != EOF) { 
            linelen++;
            // Handle empty files such that they have 0 lines
            if (first) {
                lines++;
                first = 0;
            }
        }
        // If newline char increment line count and check line length against max
        if (c == '\n') {
            // If line too long, print error message and return -1
            if (linelen > max_val) {
                fprintf(stderr, "Line %lu is too long. Max Line Length allowed for this operation is %d.\n", lines, max_val);
                return -1;
            }
            lines++;
            linelen = 0;
        }
        // If NULL char, print error mesage and return -1
        if (c == '\0') {
            fprintf(stderr, "This operation does not support NULL characters in the file.\n");
            return -1;
        }
    } while (c != EOF);

    return lines;
}

/* --- INPUT PROCESSING --- */

/*
 * Function: empty_buffer()
 * -----------------------------
 * Empties the stdin buffer so that no characters carry over to next fgets
 * call by reading each character until a newline character is read
 */
void empty_buffer() {
    char c;
    // Reads characters from stdin until end of line is reached
    while ((c = getchar()) != '\n' && c != EOF) {}
}

/*
 * Function: confirm()
 * -----------------------------
 * Prompts user to confirm and enter y or n to stdin. Whilst the input is not 
 * 'y' or 'n', user is continously prompted. Checks for validation include
 * whether EOF character was entered, whether the string is of correct length, 
 * and finally whether the string corresponds to 'y' or 'n'. Empties input
 * buffer when required to. 
 * 
 * returns: 1 if 'y' entered, else 0
 */
int confirm() {
    char buf[5];
    char c;
    // While input is invalid 
    while (1) {
        // Prompt for input
        printf("\nConfirm (y/n): ");

        // Take user input and if error or EOF, print error and exit
        if (!fgets(buf, 4, stdin)) {
            if (ferror(stdin)) {
                die("fgets");
            } else {
                fprintf(stderr, "\nEOF Character entered. Program quitting.\n");
                exit(1);
            }
        }

        // If user input is not correct length, inform user
        if (strlen(buf) != 2) {
            printf("Invalid input.\n");
            // Empty buffer if input overloaded
            if (strlen(buf) > 2 && buf[2] != '\n') empty_buffer();
            continue;
        }

        c = buf[0];
        // If input is valid option, return 1 is y and 0 if n
        if (c == 'y' || c == 'n') {
            return c == 'y' ? 1 : 0;
        // If input not valid, inform user
        } else {
            printf("Invalid input.\n");
        }
    }
}

/*
 * Function: is_number() 
 * -----------------------------
 * Checks whether all characters in a string are numerical digits.
 * Used as validation function pointer passed to input() for numeric inputs.
 * 
 * str: pointer to Char array being checked
 * 
 * returns: 0 if characters are numerical else 1 
 */
int is_number(const char* str) {
    int i;
    // Loops through char array
    for (i = 0; str[i] != '\0'; i++) {
        // If char at current index is not numerical returns 1
        if (!isdigit(str[i])) {
            return 1;
        }
    }
    // If all chars numerical return 0
    return 0;
}

/*
 * Function parse_num()
 * -----------------------------
 * Checks whether string is a valid number, and is not too long (or too short 
 * - empty stry) before attempting to parse string to unsigned long int. If any
 * checks are failed, program quits.
 * 
 * input: string to validate as numerical and convert to number 
 * maxlen: maximum allowed length of string
 * 
 * return: unsigned long int retrieved from string
 */
size_t parse_num (char *input, int maxlen) {
    // If string is not numerical, error message printed and program quits
    if (is_number(input)) {
        fprintf(stderr, "Invalid Line number Input: Non-digit\n");
        exit(1);
    }

    // If string is too long, error message printed and program quits
    if (strlen(input) > maxlen) {
        fprintf(stderr, "Invalid Line number Input: Too long\n");
        exit(1);
    }

    //If string is empty, error message printed and program quits
    if (strlen(input) < 1) {
        fprintf(stderr, "Invalid Line number Input: Empty string\n");
        exit(1);
    }

    // Attempt to convert string to unsigned long int 
    char *endptr;
    errno = 0;
    size_t line = (size_t) strtol(input, &endptr, 10);
    // If error occurred, error message printed and program quits
    if (errno) {
        fprintf(stderr, "Invalid argument for line number - must be a valid unsigned long int\n");
        die("strtol");
    }

    return line;
}

/*
 * Function valid_fname()
 * -----------------------------
 * Checks whether provided string is a valid file path to be created using a 
 * regex expression. File names can start with alphanumerics and select symbols
 * including: (.) (_) (-); spaces can occur in the middle of a file name. 
 * 
 * input: file path being checked for validity
 */
void valid_fname (char *input) {
    regex_t reg;
    int temp;

    // Compiles regex expression to check for valid file path
    if (temp = regcomp(&reg, FNAME, REG_ICASE | REG_EXTENDED | REG_NOSUB)) {
        // If error, error message printed and program quits
        char *buffer = (char *) malloc(MAX);
        regerror(temp, &reg, buffer, sizeof(buffer));
        fprintf(stderr,"grep: %s (%s)\n", buffer, FNAME);
        free(buffer);
        exit(1);
    }

    // Runs the compiled regex pattern on the string to check for matches
    if (regexec(&reg, input, 0, NULL, 0) != 0) {
        // If no matches, error message printed and program quits
        printf("Invalid filename\n");
        regfree(&reg);
        exit(1);
    }

    regfree(&reg);
}

/*
 * Function parse_string()
 * -----------------------------
 * Checks whether length of string is within a maximum and minimum length limit. 
 * If checks failed, program quits. 
 */
void parse_string (char *input, int maxlen, int minlen, int arg) {
    // If string is too long, error message printed and program quits
    if (strlen(input) > maxlen) {
        fprintf(stderr, "Invalid Input (Argument %d): Too long\n", arg);
        exit(1);
    }

    // If string is too short, error message printed and program quits
    if (strlen(input) < minlen) {
        fprintf(stderr, "Invalid Input (Argument %d): Too short\n", arg);
        exit(1);
    }
}

/* --- CHANGE LOG --- */

/*
 * Function truncate_log()
 * -----------------------------
 * Ensures that the log file does not get too large, by checking how many logs 
 * there are, and if it exceeds the predefined limit, old logs are removed such
 * that there are 10 less than the limit. Before reading, validates that log 
 * file is safe for reading using fgets().
 * If there are any errors, any open files are closed, valid error messages are 
 * printed and the program safely quits. 
 */
void truncate_log() {
    // Attempts to open log file in read mode (with error handling)
    FILE *fptr = fopen(LOGF, "r");
    if (!fptr) die("fopen log");

    ssize_t logs;
    // Obtain number of logs in log file and check if log file is safe for fgets()
    if ((logs = verify_lines(&fptr, LOGLEN - 2)) == -1){
        // If not safe for fgets, user prompted and program quits
        puts("Warning: Log file has been edited by another program. Modify file to meet constraint or Delete file.\n");
        fclose(fptr);
        exit(1);
    }

    // If logs exceed limit
    if (logs > CLOG_BUFFER) {
        // Reset file pointer to start of file (with error handling)
        if (fseek(fptr, 0, SEEK_SET)) {
            fclose(fptr);
            die("fseek");
        }
        
        // Attempts to open temp file in write mode (with error handling)
        FILE *temp = fopen(TEMPF, "w");
        if (!temp) {
            fclose(fptr);
            die("fopen temp");
        }

        char *line = (char *) malloc(LOGLEN);

        // Read lines from log file until End Of File
        while (fgets(line, LOGLEN - 1, fptr) != NULL) {
            // Write line to temp file if, number of remaing logs is 10 less than the limits
            if (logs > CLOG_BUFFER - 10) --logs;
            else fputs(line, temp);
        }
        
        // Close both log and temp files
        fclose(temp);
        fclose(fptr);

        // Attempts to delete original log file (with error handling)
        if (remove(LOGF)) {
            fprintf(stderr, "Error removing original log file. Warning there will be temp files remaining.\n");
            die("remove");
        }

        // Attempts to rename temp file to replace log file (with error handling)
        if (rename(TEMPF, LOGF)) {
            fprintf(stderr, "Error renaming temp file. Warning temp file will be remaining.\n");
            die("rename");
        }

    // Otherwise, if logs does not exceed limit, close log file
    } else {
        fclose(fptr);
    }
}

/*
 * Function: change_log()
 * -----------------------------
 * Write the provided logstr argment to the end of the log file (if log file 
 * does not exist, it is created). The logstr is written alongside a timestamp. 
 * After writing, truncateLog() is called to ensure the log file does not become
 * too long.
 * 
 * logstr: formatted log string (describing operation) to be appended to log file 
 */
void change_log(char *logstr) {
    // Retrieve current time
    time_t t = time(NULL);
    struct tm* timeinfo;
    timeinfo = localtime(&t);
    
    // Attempt to open log file in append plus mode (with error handling)
    FILE *fptr = fopen(LOGF, "a+");
    if (!fptr) die("fopen log");

    // Add time in correct format with log string to the end of the log file
    fprintf(fptr, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, 
        timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, logstr);
    
    // Close the log file
    fclose(fptr);
    // Manage log file length
    truncate_log();
}

/*
 * Function display_log()
 * -----------------------------
 * Verifies the log file is safe for reading with fgets and then proceeds to 
 * read logs from the log file. If a file path is specified, then only logs 
 * related to the file path will be printed, otherwise if the file path is NULL,
 * all the logs will be printed. This is done by searching for a search key in 
 * each of the lines and ensuring it occurs before any quotation marks used to 
 * encase literal strings that were added to the files.
 * 
 * fpath: path to file for which the change log is to be displayed (if NULL all
 * logs will be displayed)
 */
void display_log(char *fpath) {
    // If log file does not exist (or cannot be accessed), error message is printed and program quits
    if (access(LOGF, F_OK)) {
        printf("Log file does not exist.\n");
        exit(1);
    }

    // Attempt to open log file in read mode (with error handling)
    FILE *fptr = fopen(LOGF, "r");
    if (!fptr) die("fopen log");

    // If log file is not safe for fgets(), error message is printed and program quits
    if (verify_lines(&fptr, LOGLEN - 2) == -1){
        puts("Warning: Log file has been edited by another program. Modify file to meet constraint or Delete file.\n");
        fclose(fptr);
        exit(1);
    }

    // File pointer is reset to start of file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    // If fpath is not NULL, a search key is constructed 
    char key[MAX];
    if (fpath != NULL) {
        snprintf(key, MAX, "File \'%s\'", fpath);    
    }

    char *line = (char *) malloc(LOGLEN);
    char *found;
    int index;
    
    // Read lines from log file until End Of File
    while (fgets(line, LOGLEN - 1, fptr) != NULL) {
        // If fpath is NULL, print all lines
        if (fpath == NULL) {
            printf("%s", line);
        // Otherwise, if search key found in line
        } else if ((found = strstr(line, key)) != NULL) {
            // Ensure that the search key occurs before any quotation marks
            index = found - line;
            if ((found = strchr(line, '\"')) != NULL) {
                if (index < found - line) {
                    printf("%s", line);    
                }
            } else {
                printf("%s", line);
            }
        }
    }

    free(line);
    // Close the log file
    fclose(fptr);
}

/* --- FILE OPERATIONS --- */

/*
 * Function create_file()
 * -----------------------------
 * Creates a file with at the provided fpath location. If a file already exists 
 * and is a regular file, the user is asked to confirm the overwritting of the 
 * file. If the file is not a regular file, the program quits and if the file 
 * does not exist, the validity of the new file name is checked using 
 * valid_fname(). If successful, logs operation to log file with change_log() 
 * 
 * fpath: path to new file being created/overwritten
 */
void create_file(char *fpath) {
    // If the file exists
    if (!access(fpath, F_OK)) {
        // And the file is a regular file, user is prompted to confirm overwriting
        if (is_file(fpath)) {
            printf("File \'%s\' already exists and will be overwritten.", fpath);
            // If user does not confirm, program quits
            if (!confirm()) {
                printf("Overwrite aborted.\n");
                exit(1);
            }
        // If file is not a regular file, error message is printed and program quits
        } else {
            printf("File path refers to non-regular file and cannot be modified.\n");
            exit(1);
        }
    // If file does not exist, validity of file path is checked
    } else {
        valid_fname(fpath);
    }

    // Attempts to open file in write mode (with error handling)
    FILE *fptr = fopen(fpath, "w");
    if (!fptr) die("fopen");

    // Closes file
    fclose(fptr);

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\' created/overwritten | Lines After = 0", fpath);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/*
 * Function: del_file()
 * -----------------------------
 * Deletes the file that exists at the file path. If an error occurs, program 
 * quits. If successful, logs operation to log file with change_log(). 
 * 
 * fpath: path to file to be deleted
 */
void del_file(char *fpath) {
    // Attempts to delete file at fpath (with error handling)
    if (remove(fpath)) {
        die("remove");
    }

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\' deleted  | Lines After = n/a", fpath);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/*
 * Function: copy_file()
 * -----------------------------
 * Copies the contents from one file to another file. If the destination file
 * exists and is a regular file, the user is asked confirm the overwriting of 
 * the file. If the file is not a regular file, the program quits and if the 
 * file does not exist, the validty of the new filename is checked using 
 * valid_fname(). Copies contents from one file to another character by 
 * character, by opening the source in read mode and destination in read mode. 
 * If successful, logs operation to log file with change_log(). 
 * 
 * fpath1: path to file to be copied from (source)
 * fpath2: path to file to be copied to (destination)
 */
void copy_file(char *fpath1, char *fpath2) {
    // If the destination file exists
    if (!access(fpath2, F_OK)) {
        // And the dest file is a regular file, user is prompted to confirm overwriting
        if (is_file(fpath2)) {
            printf("File \'%s\' already exists and will be overwritten.", fpath2);
            // If user does not confirm, program quits
            if (!confirm()) {
                printf("Overwrite aborted.\n");
                exit(1);
            }
        // If dest file is not a regular file, error message is printed and program quits
        } else {
            printf("File path refers to non-regular file and cannot be modified.\n");
            exit(1);
        }
    // If destination file does not exist, validty of file path is checked
    } else {
        valid_fname(fpath2);
    }

    // Attempts to open source file in read mode (with error handling)
    FILE *fptr1 = fopen(fpath1, "r");
    if (!fptr1) die("fopen src");

    // Counts the number of lines in the source file for logging purposes
    size_t lines = count_lines(&fptr1);

    // Resets the file pointer to the start (with error handling)
    if (fseek(fptr1, 0, SEEK_SET)) {
        fclose(fptr1);
        die("fseek");
    }

    // Attemps to open destination file in write mode (with error handling)
    FILE *fptr2 = fopen(fpath2, "w");
    if (!fptr2) {
        fclose(fptr1);
        die("fopen dst");
    }

    // Reads char from source and writes to destination, while end of file not reached
    char c = getc(fptr1);
    while (c != EOF) {
        fputc(c, fptr2);
        c = getc(fptr1);
    }

    // Closes both source and destination files 
    fclose(fptr1);
    fclose(fptr2);

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\' copied to \'%s\' | Lines After = %lu", fpath1, fpath2, lines);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/*
 * Function: show_file()
 * -----------------------------
 * Displays the contents of the specified file with line numbers along the left
 * side. Prints line numbers in an alligned formatted way. Reads characters one 
 * at a time and prints them to the screen, until the end of file is reached. If
 * a newline is reached, the line number is printed. 
 * 
 * fpath: path to file to be displayed to the command line
 */
void show_file(char *fpath) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");

    // Counts the number of lines in the file 
    size_t lines = count_lines(&fptr);
    // Finds the number of digits needs to display the line numbers
    int digits = 1;
    while (lines > 9) {
        lines /= 10;
        digits ++;
    }

    // Resets the file pointer to the start of the file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    char c; 
    lines = 1;
    printf("%0*lu |", digits, lines);

    // Reads char from file and prints char while end of file is not reached
    do {
        c = fgetc(fptr);
        if (c != EOF) printf("%c", c);
        // If newline char read, increment line number and print formatted line number
        if (c == '\n') {
            lines++;
            printf("%0*lu |", digits, lines);
        }
    } while (c != EOF);

    printf("\n");

    // Close the file
    fclose(fptr);
}

/* --- LINE OPERATIONS --- */

/*
 * Function: append_line()
 * -----------------------------
 * Write the provided string to a newline at the end of the provided file. The 
 * file is opened is append mode to write the string to the end of the file. If
 * the file is not empty, a newline character is added to ensure the string is 
 * written on a newline. The file is then reopened in read mode to count the 
 * number of lines for logging purposes. If successful, logs operation to log 
 * file with change_log(). 
 * 
 * fpath: path to file to which the line is appended to
 * line: string which is to be written to end of file
 */
void append_line(char *fpath, char *line) {
    // Attempts to open file in append mode (with error handling)
    FILE *fptr = fopen(fpath, "a");
    if (!fptr) die("fopen");

    // If the file is empty, the line is written to the end of the file
    if (is_empty(fpath)) fprintf(fptr, "%s", line);
    // Else the line is written with a newline character 
    else fprintf(fptr, "\n%s", line);

    // File is closed
    fclose(fptr);

    // Attempts to open file in read mode (with error handling)
    fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");

    // Number of lines in file after append is counted for logging purposes
    size_t lines = count_lines(&fptr);
    
    // File is closed 
    fclose(fptr);

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\': Line \"%s\" appended | Lines After = %lu", fpath, line, lines);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/*
 * Function: show_line()
 * -----------------------------
 * Displays the line specified by the provided line number in the specified file.
 * Ensures that the line number is within the range of the number of lines in
 * the file. Proceeds to read characters one-by-one, until the required line is 
 * reached, after which the chars are printed to the screen, until the next line
 * or end of file is reached 
 * 
 * fpath: path to file from which line is being displayed
 * lineno: position of line to be displayed in file
 */
void show_line(char *fpath, size_t lineno) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");

    // If provided lineno is greater than total lines in file
    if (lineno > count_lines(&fptr)) {
        // Error message is printed and program quits
        printf("Invalid Input: Line number out of range for file.\n");
        fclose(fptr);
        exit(1);
    }

    // Resets file pointer to start of file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    char c;
    size_t lines = 1;
    // Reads characters from file, one by one, until required line is reached
    while (lines != lineno) {
        c = fgetc(fptr);
        // If newline char read, lines counter is increased
        if (c == '\n') lines ++;
    }
    
    // Reads chars from file and prints them to screen until end of file or line is reached
    do {
        c = fgetc(fptr);
        // Prints non-special characters
        if (c != '\n' && c != '\r' && c != EOF) printf("%c", c);
    } while (c != '\n' && c != EOF);

    printf("\n");

    // Closes the file
    fclose(fptr);
}

/*
 * Function: del_line()
 * -----------------------------
 * Deletes the line specified by the provided line number in the specified file. 
 * Ensures that the line number is within the range of the number of lines in
 * the file. Opens a temporary file to write to and proceeds to read chars from 
 * the original file one-by-one, writing them to the temp file, as long as the 
 * line number was not the provided line number. This is done until the end of 
 * the file, after which the original file is removed and the temporary file is 
 * renamed to replace the original file. If successful, logs operation to log 
 * file with change_log(). 
 * 
 * fpath: path to file from which line will be deleted
 * lineno: position of line to delete in file
 */
void del_line(char *fpath, size_t lineno) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");

    size_t lines;
    // Obtains number of lines in file and If provided lineno is greater,
    if (lineno > (lines = count_lines(&fptr))) {
        // Error message is printed and program quits
        printf("Invalid Input: Line number out of range for file.\n");
        fclose(fptr);
        exit(1);
    }

    // Resets file pointer to start of file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    // Attempts to open temp file in write mode (with error handling)
    FILE *temp = fopen(TEMPF, "w");
    if (!temp) {
        fclose(fptr);
        die("fopen temp");
    }

    size_t count = 1;
    char c;
    // While End-Of-File is not reached, 
    do {
        // Read char from original file and increment line count if newline
        c = getc(fptr);
        if (c == '\n') count++;
        // If line count is not specified lineno (and EOF not reached), write char to temp file
        if (count != lineno && c != EOF){
            if (count != lineno + 1 || c != '\n') fputc(c, temp);
        }
    } while (c != EOF);

    // Close both original and temp files
    fclose(fptr);
    fclose(temp);

    // Attempts to delete original file (with error handling)
    if (remove(fpath)) {
        fprintf(stderr, "Error removing original file. Warning there will be temp files remaining.\n");
        die("remove");
    }

    // Attempts to rename temp file to replace original file (with error handling)
    if (rename(TEMPF, fpath)) {
        fprintf(stderr, "Error renaming temp file. Warning temp file will be remaining.\n");
        die("rename");
    }

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\': Line %lu deleted | Lines After = %lu", fpath, lineno, lines - 1);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/*
 * Function: ins_line()
 * -----------------------------
 * Inserts the line specified at the provided line number in the specified file.
 * Ensures that the line number is within the range of the number of lines in
 * the file. Opens a temporary file to write to and proceeds to read chars from
 * the original file one-by-one, writing them to the temp file. When the 
 * provided line number is reached, the specified string is written to the file, 
 * after which characters are read from the original and written to the temp 
 * file until the end of file is reached. The original file is then removed and
 * the temporary file is renamed to replace the original file. If successful, 
 * logs operation to log file with change_log(). 
 * 
 * fpath: path to file to which line will be inserted
 * line: string to be inserted to file 
 * lineno: position at which line will be inserted in file
 */
void ins_line(char *fpath, char *line, size_t lineno) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");


    size_t lines;
    // Obtains number of lines in file and If provided lineno is greater,
    if (lineno > (lines = count_lines(&fptr))) {
        // Error message is printed and program quits
        printf("Invalid Input: Line number out of range for file.\n");
        fclose(fptr);
        exit(1);
    }

    // Resets file pointer to start of file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }
    
    // Attempts to open temp file in write mode (with error handling)
    FILE *temp = fopen(TEMPF, "w");
    if (!temp) {
        fclose(fptr);
        die("fopen temp");
    }

    size_t count = 1;
    char c;

    // While End-Of-File is not reached
    do {
        // If line count is not the specified lineno
        if (count != lineno) {
            // Read char from original file and write to temp (if not end of file)
            c = getc(fptr);
            if (c != EOF) fputc(c, temp);
            // Increment line counter if newline char
            if (c == '\n') count++;
        }
        // If specified line reached, write specified string to temp file, and continue
        if (count == lineno) {
            fprintf(temp, "%s\n", line);
            count++;
        }
    } while (c != EOF);

    // Close both the original and temp file 
    fclose(fptr);
    fclose(temp);

    // Attempts to delete original file (with error handling)
    if (remove(fpath)) {
        fprintf(stderr, "Error removing original file. Warning there will be temp files remaining.\n");
        die("remove");
    }

    // Attempts to rename temp file to replace original file (with error handling)
    if (rename(TEMPF, fpath)) {
        fprintf(stderr, "Error renaming temp file. Warning temp file will be remaining.\n");
        die("rename");
    }

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\': Line \"%s\" inserted at Line %lu | Lines After = %lu", fpath, line, lineno, lines + 1);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/*
 * Function: rep_line()
 * -----------------------------
 * Replaces the line specified at te provided line number in the specified file. 
 * Ensures that the line number is within the range of the number of lines in
 * the file. Opens a temporary file to write to and proceeds to read chars from
 * the original file one-by-one, writing them to the temp file. When the 
 * provided line number is reached, the specified string is written to the file, 
 * and the original file pointer is moved to the end of that line, after which 
 * characters are read from the original and written to the temp file until the
 * end of file is reached. The original file is then removed and the temporary 
 * file is renamed to replace the original file. If successful, logs operation 
 * to log file with change_log(). 
 * 
 * fpath: path to file in which line will be replaced
 * line: string to replace existing line in file
 * lineno: position of the line to be replaced in file
 */
void rep_line(char *fpath, char *line, size_t lineno) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");


    size_t lines;
    // Obtains number of lines in file and If provided lineno is greater,
    if (lineno > (lines = count_lines(&fptr))) {
        // Error message is printed and program quits
        printf("Invalid Input: Line number out of range for file.\n");
        fclose(fptr);
        exit(1);
    }

    // Resets file pointer to start of file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    // Attempts to open temp file in write mode (with error handling)
    FILE *temp = fopen(TEMPF, "w");
    if (!temp) {
        fclose(fptr);
        die("fopen temp");
    }

    size_t count = 1;
    char c;

    // While End-Of-File is not reached
    do {
        // If line count is not the specified lineno
        if (count != lineno) {
            // Read char from original file and write to temp (if not end of file)
            c = getc(fptr);
            fputc(c, temp);
            // Increment line counter if newline char
            if (c == '\n') count++;
        }
        if (count == lineno) {
            // Move file pointer to end of line in original file
            do {
                c = getc(fptr);
            } while (c != '\n' && c != EOF);
            //  Write specified string to tmp file
            fputs(line, temp);
            if (c == '\n') fputc(c, temp);
            count++;
        }
    } while (c != EOF);

    // Close both the original and temp file 
    fclose(fptr);
    fclose(temp);

    // Attempts to delete original file (with error handling)
    if (remove(fpath)) {
        fprintf(stderr, "Error removing original file. Warning there will be temp files remaining.\n");
        die("remove");
    }

    // Attempts to rename temp file to replace original file (with error handling)
    if (rename(TEMPF, fpath)) {
        fprintf(stderr, "Error renaming temp file. Warning temp file will be remaining.\n");
        die("rename");
    }

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\': Line %lu was replaced by \"%s\" | Lines After = %lu", fpath, lineno, line, lines);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/* --- OTHER OPERATIONS --- */

/*
 * Function: search()
 * -----------------------------
 * Searches for specified string in file. Verifies that the file is safe for 
 * reading with fgets(). Finds the number of digits to use to display the line
 * numbers aligned on the left. Proceeds to read lines from the file and checks 
 * for instances of the search key in each line. If found, the number of 
 * instances, the line number and the line itself are printed. Once all lines 
 * are read and the end of file is reached, the total number of occurences of 
 * the search key in the file is printed. 
 * 
 * fpath: path to file in which to search for string
 * key: string to search for in file
 */
void search(char *fpath, char *key) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");


    ssize_t lines;
    // Obtain number of logs in log file and check if log file is safe for fgets()
    if ((lines = verify_lines(&fptr, MAX - 2)) == -1) {
        // If not safe for fgets, user prompted and program quits
        fclose(fptr);
        exit(1);
    }

    // Finds the number of digits needs to display the line numbers
    int digits = 1;
    while (lines > 9) {
        lines /= 10;
        digits ++;
    }

    // Resets the file pointer to the start of the file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    char *line = (char *) malloc(MAX);
    char const *buffer;
    int count = 0;
    int linelen;
    int subcount;
    lines = 0;

    // Reads line from the file until the End-Of-File
    while (fgets(line, MAX - 1, fptr) != NULL) {
        // Increment line counter
        lines++;

        // Remove trailing newline char
        linelen = strlen(line);
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        if (linelen != MAX) line[linelen] = '\0';

        buffer = line;
        subcount = 0;
        // Count number of instances of search key in line
        while((buffer = strstr(buffer, key)) != NULL) {
            buffer += strlen(key);
            subcount++;
        }

        // Increment count by number of occurrences
        count += subcount;

        // If instance of search key found in line, print line with number of instances
        if (subcount != 0){
            printf("%d instance/s:\n%0*lu |%s\n\n", subcount, digits, lines, line);
        }
    }

    free(line);

    // Close the file
    fclose(fptr);

    // Print total instances of search key in file
    printf("%d instance/s found in the file.\n", count);
}

/*
 * Function: regex_search()
 * -----------------------------
 * Searched for specified regex pattern in file. Verifies that the file is safe 
 * for reading with fgets(). Finds the number of digits to use to display the 
 * line numbers aligned on the left. Compiles the regex string to a pattern and 
 * handles any errors during this process. Proceeds to read lines from file and 
 * run the regex pattern on the lines. If a match is found in the line, the 
 * line is printed with the line number in a well-formatted way. Once the end of
 * file is reached, the total number of matches made in the file is printed.
 * 
 * fpath: path to file in which to search for regex matches
 * key: regex expression as string
 */
void regex_search(char *fpath, char *key) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");

    ssize_t lines;
    // Obtain number of logs in log file and check if log file is safe for fgets()
    if ((lines = verify_lines(&fptr, MAX - 2)) == -1) {
        // If not safe for fgets, user prompted and program quits
        fclose(fptr);
        exit(1);
    }

    // Finds the number of digits needs to display the line numbers
    int digits = 1;
    while (lines > 9) {
        lines /= 10;
        digits ++;
    }

    // Resets the file pointer to the start of the file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    char *buffer = (char *) malloc(MAX);

    regex_t reg;
    int temp;
    // Attempts to compiles regex expression provided
    if (temp = regcomp(&reg, key, REG_EXTENDED | REG_NOSUB | REG_ICASE)) {
        // If error, error message printed and program quits
        regerror(temp, &reg, buffer, sizeof(buffer));
        fprintf(stderr,"grep: %s (%s)\n", buffer, key);
        fclose(fptr);
        exit(1);
    }

    int count = 0;
    int linelen;
    lines = 0;

    // Reads lines from file until End-Of-File
    while (fgets(buffer, MAX - 1, fptr) != NULL) {
        // Increment line counter
        lines++;

        // Removing trailing newline char
        linelen = strlen(buffer);
        while (linelen > 0 && (buffer[linelen - 1] == '\n' || buffer[linelen - 1] == '\r')) linelen--;
        if (linelen != MAX) buffer[linelen] = '\0';

        // Runs the compiled regex pattern on the LINE to check for matches
        if ((temp = regexec(&reg, buffer, 0, NULL, 0)) == 0) {
            // If matches found, increment counter and print line
            count++;
            printf("%0*lu |%s\n\n", digits, lines, buffer);
        }

    }

    free(buffer);
    regfree(&reg);
    // Close file
    fclose(fptr);

    // Print total matches to regex pattern found in the file
    printf("%d line matches found in the file.\n", count);
}

/*
 * Function: string_sub()
 * -----------------------------
 * Substitutes all elements of a specified key substring with another substring 
 * in a provided line string. Allocates enough memory in order to hold 
 * substituted string (with error handling if it fails). Proceeds to build 
 * string, by copying parts from the original string, and replacing any 
 * occurence of the key substring with the replacement substring, and moving 
 * along the original string using pointers. A pointer to the result is then 
 * returned (must be freed outside of function in appropriate place)
 * 
 * line: string in which replacements will be done
 * key: strings whose instances will be replaced
 * sub: string with which to replace instances of key substring
 * occur: number of replacements to make
 * 
 * returns: pointer to resulting string
 */
char *string_sub(char *line, char *key, char *sub, int occur) {
    int len_key = strlen(key);
    if (len_key == 0) return NULL;
    int len_sub = strlen(sub);

    // Allocate required memory for resulting string (with error handling)
    char *result = (char *) malloc(strlen(line) + (len_sub - len_key) * occur + 1);
    if (!result) return NULL;
    
    char *pos;
    char *tmp = result;
    int offset;

    // Whilst more replacements need to be made
    while (occur--) {
        // Find next occurence of key substring in remainder of original line
        pos = strstr(line, key);
        offset = pos - line;
        // Copy substring before instance into to result string
        tmp = strncpy(tmp, line, offset) + offset;
        // Copy replacement substring into result string
        tmp = strcpy(tmp, sub) + len_sub;
        // Move along original line pointer
        line += offset + len_key; 
    }
    strcpy(tmp, line);

    return result;
}

/*
 * Function: replace()
 * -----------------------------
 * Replaces are instances of a provided key substring with another provided 
 * substring in a file. Verifies that the file is safe for reading with fgets().
 * Finds the number of digits to use to display the line numbers aligned on the
 * left. Opens a temporary file to write to and reads line from the original 
 * file. If an instance of the key substring is found in the line, the number of
 * instances are counted and then substitutions are made by calling string_sub()
 * and the modified line is written to the temp file and the modification is 
 * also printed. If no modification are made, the line is written as is to the 
 * temp file. Once the end of file is reached, the number of instances replaced 
 * is printed. The original file is then removed and the temporary file is 
 * renamed to replace the original file. If successful, logs operation to the 
 * log file with change_log(). 
 */
void replace(char *fpath, char *key, char *sub) {
    // Attempts to open file in read mode (with error handling)
    FILE *fptr = fopen(fpath, "r");
    if (!fptr) die("fopen");

    ssize_t lines;
    // Obtain number of logs in log file and check if log file is safe for fgets()
    if ((lines = verify_lines(&fptr, MAX - 2)) == -1){
        // If not safe for fgets, user prompted and program quits
        fclose(fptr);
        exit(1);
    }

    size_t total = lines;

    // Finds the number of digits needs to display the line numbers
    int digits = 1;
    while (lines > 9) {
        lines /= 10;
        digits ++;
    }

    // Resets the file pointer to the start of the file (with error handling)
    if (fseek(fptr, 0, SEEK_SET)) {
        fclose(fptr);
        die("fseek");
    }

    // Attempts to open temp file in write mode (with error handling)
    FILE *temp = fopen(TEMPF, "w");
    if (!temp) {
        fclose(fptr);
        die("fopen temp");
    } 

    char *line = (char *) malloc(MAX);
    char *buffer;
    char *result;
    size_t linelen;
    int subcount;
    int count = 0;
    lines = 0;

    // Reads lines from original file until End-Of-File
    while (fgets(line, MAX - 1, fptr) != NULL) {
        // Incrmeent line counter
        lines++;

        buffer = line;
        // If instance of key substring found in line
        if ((buffer = strstr(buffer, key)) != NULL) {
            // Count number of instance of key substring in line
            subcount = 1;
            buffer += strlen(key);
            while ((buffer = strstr(buffer, key)) != NULL) {
                subcount++;
                buffer += strlen(key);
            }

            // Increment count by number of occurrences
            count += subcount;

            // Removing trailing newline chars
            linelen = strlen(line);
            while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
            if (linelen != MAX) line[linelen] = '\0';

            // Print line before substition
            printf("%d substitution\\s:\n", subcount);
            printf("%0*lu |%s\n", digits, lines, line);

            // Make all substitions in line (with error handling)
            if ((result = string_sub(line, key, sub, subcount)) == NULL) {
                fclose(fptr);
                fclose(temp);
                fprintf(stderr, "\nError replacing string. Warning: Temporary files will remain.\n");
                exit(1);
            }

            // Write modified to temp file and print
            fprintf(temp, "%s\n", result);
            printf(" to\n%0*lu |%s\n\n", digits, lines, result);
            free(result);
        // Else if no instance, write unmodified line to temp file
        } else {
            fputs(line, temp);
        }
    }

    free(line);
    // Print total number of replacement made in file 
    printf("%d instances replaced in the file.\n", count);

    // Close both the original and temp file 
    fclose(fptr);
    fclose(temp);
    
    // Attempts to delete original file (with error handling)
    if (remove(fpath)) {
        fprintf(stderr, "Error removing original file. Warning there will be temp files remaining.\n");
        die("remove");
    }

    // Attempts to rename temp file to replace original file (with error handling)
    if (rename(TEMPF, fpath)) {
        fprintf(stderr, "Error renaming temp file. Warning temp file will be remaining.\n");
        die("rename");
    }

    // Creates log string describing operation and number of lines after operation
    char *msg = (char *) malloc(LOGLEN);
    snprintf(msg, LOGLEN, "File \'%s\': Instances of \"%s\" replaced by \"%s\" | Lines After = %lu", fpath, key, sub, total);
    // Appends log string to log file
    change_log(msg);
    free(msg);
}

/* --- USAGE --- */

/*
 * Function: usage()
 * -----------------------------
 * Prints how the program is to be used, with the different flags, what they do,
 * the arguments they require and any additional infomration about the program 
 * as well. After printing the program quits.  
 */
void usage() {
    printf("Simple Text Editor\n\nUSAGE\n./editor [OPTION] [ARGUMENTS]...\n\nOPTIONS\n");
    printf("-cr <file>\n    create empty file (will overwrite if file exists)\n\n");
    printf("-dl <file>\n    delete existing file\n\n");
    printf("-cp <src> <dst>\n    copy existing file from source path to destination path\n\n");
    printf("-sh <file>\n    display contents of file with line numbers\n\n");
    printf("-lsh <file> <linenum>\n    display specified line of file\n\n");
    printf("-la <file> <line>\n    append line to end of file on new line\n\n");
    printf("-ldl <file> <linenum>\n    delete specified line of file\n\n");
    printf("-lin <file> <line> <linenum>\n    insert line into specified position in file\n\n");
    printf("-lrp <file> <line> <linenum>\n    replace line at linenum in file with given string\n\n");
    printf("-sch <file> <key>\n    search for string in file\n\n");
    printf("-schreg <file> <key>\n    regex search in file [RegEx Standard depends on System - POSIX on most linux]\n\n");
    printf("-rp <file> <key> <sub>\n    replace all occurences of <key> with <sub>\n\n");
    printf("-chlog <file>\n    display change log (will display universal change log, if no file specified)\n\n");
    printf("-cl <file>\n    display number of lines in file (0 if empty)\n\n");
    printf("EXAMPLES\n./editor -cr foo.bar\n./editor -la ../foo.txt \"THE END\"\n");
    printf("./editor -cp foo.c ../foo/bar/out.c\n./editor -lin foo.c \"The New Beginning\" 1\n");
    printf("./editor -sch foo.c the\n\nNOTE\n");
    printf("Program only works with regular files and program must have permission to read/write ");
    printf("read/write to file depending on operation. Please ensure temp file used by program is not in use.\n");
    printf("Temp File: %s\tLog File: %s\nMax File-path Len: %d\t", TEMPF, LOGF, MAXF);
    printf("\tMax String Len: %d\nMax Regex String Len: %d\tMax Number of Logs Kept: %d\n", MAX, MAXF, CLOG_BUFFER);
    exit(1);
}

/* --- MAIN --- */

/*
 * Function: main()
 * -----------------------------
 * Validates the number of arguments as well as all the arguments itself, with 
 * some general validations and some specific validations for each of the 
 * operations. Contains mappings from the flags in the command line arguments to
 * the corresponding function to be called. Any incorrect arguments or number of
 * arguments result in usage() being called to inform user how to use program.
 * 
 * argc: number of command line arguments passed into program
 * argv: array of command line arguments read from terminal
 * 
 * returns: 
 */
int main(int argc, char *argv[]) {
    // If there are too little or too many arguments, user is shown how to use program
    if (argc < 2 || argc > 5) usage();

    int flag = strlen(argv[1]);
    // If flag argument is too short (or long) or doesn't start with '-', user is shown how to use program
    if (flag < 3 || argv[1][0] != '-' || flag > 7) usage();

    // For all operations other than change log
    if (strcmp(argv[1], "-chlog")) {
        // If not enough arguments given, user is shown how to use program
        if (argc < 3) usage();

        // The second argument is Validated as file path string
        parse_string(argv[2], MAXF, 1, 2);

        // For all operations other than create 
        if (strcmp(argv[1], "-cr")) {
            // If file cannot be accessed (or doesn't exist), program quits with error message
            if (access(argv[2], F_OK)) {
                fprintf(stderr, "Given file path either does not exist or cannot be accessed.\n");
                return 1;
            }
            // If file is not a regular file, program quits with error message
            if (!is_file(argv[2])) {
                fprintf(stderr, "Given file path refers to non-regular file.\n");
                return 1;
            }
        }
    }

    // Switch statement for first letter of flag argument
    // For each flag, if correct number of arguments is not provided, user is shown how to use program
    switch (argv[1][1]) {
        case 'l':
            if (!strcmp(argv[1], "-la")) {

                if (argc != 4) usage();
                // Validate string to append and call append line with validated arguments
                parse_string(argv[3], MAX, 0, 3);
                append_line(argv[2], argv[3]);

            } else if (!strcmp(argv[1], "-lsh")) {

                if (argc != 4) usage();
                // Parse line number to show and call show line with validated arguments
                size_t line = parse_num(argv[3], 20);
                show_line(argv[2], line);

            } else if (!strcmp(argv[1], "-ldl")) {

                if (argc != 4) usage();
                // Parse line number to delete and call delete line  with validated arguments
                size_t line = parse_num(argv[3], 20);
                del_line(argv[2], line);

            } else if (!strcmp(argv[1], "-lin")) {

                if (argc != 5) usage();
                // Validate string to insert and parse line number to insert string at
                parse_string(argv[3], MAX, 0, 3);
                size_t line = parse_num(argv[4], 20);
                // Call insert line with validated arguments
                ins_line(argv[2], argv[3], line);
                
            } else if (!strcmp(argv[1], "-lrp")) {

                if (argc != 5) usage();
                // Validate string to replace line and parse line number to replace with string
                parse_string(argv[3], MAX, 0, 3);
                size_t line = parse_num(argv[4], 20);
                // Call replace line with validated arguments
                rep_line(argv[2], argv[3], line);


            } else {
                usage();
            }

            break;
        case 'c':
            // Ensures flag is correct
            if (flag != 3 && strcmp(argv[1], "-chlog")) usage();
            switch (argv[1][2]) {
                case 'r':

                    if (argc != 3) usage();
                    // Call create file with validated argument
                    create_file(argv[2]);
                    break;

                case 'p':

                    if (argc != 4) usage();
                    // Validate destination file path and call copy file with validated arguments
                    parse_string(argv[3], MAXF, 1, 3);
                    copy_file(argv[2], argv[3]);
                    break;

                case 'l':

                    if (argc != 3) usage();
                    // Attempts to open file in read mode (with error handling)
                    FILE *fptr = fopen(argv[2], "r");
                    if (!fptr) die("fopen");
                    // Counts number of lines and prints it
                    printf("\'%s\' has %lu lines\n", argv[2], count_lines(&fptr));
                    // Closes the file
                    fclose(fptr);
                    break;

                case 'h':

                    if (!strcmp(argv[1], "-chlog")) {

                        if (argc != 2 && argc != 3) usage();
                        // If file is specified, validate file path string
                        if (argc == 3) {
                            parse_string(argv[2], MAXF, 1, 2);
                            // Call display change log with validated argument
                            display_log(argv[2]);
                        // If no file specified, call display change log with NULL argument
                        } else {
                            display_log(NULL);
                        }
                        break;

                    }

                default:
                    usage();
            }
            break;
        case 's':
            if (!strcmp(argv[1], "-sh")) {

                if (argc != 3) usage();
                // Call show file with validated argument
                show_file(argv[2]);

            } else if (!strcmp(argv[1], "-sch")) {

                if (argc != 4) usage();
                // Validate search string and call search with validated arguments
                parse_string(argv[3], MAX, 1, 3);
                search(argv[2], argv[3]);

            } else if (!strcmp(argv[1], "-schreg")) {

                if (argc != 4) usage();
                // Validate regex string and call regex search with validated arguments
                parse_string(argv[3], MAXF, 1, 3);
                regex_search(argv[2], argv[3]);

            } else {
                usage();
            }

            break;
        default:
            if (!strcmp(argv[1], "-dl")) {

                if (argc != 3) usage();
                // Call delete file with validated argument
                del_file(argv[2]);

            } else if (!strcmp(argv[1], "-rp")) {

                if (argc != 5) usage();
                // Validate search and replace substrings and call replace with validated arguments
                parse_string(argv[3], MAX, 1, 3);
                parse_string(argv[4], MAX, 0, 4);
                replace(argv[2], argv[3], argv[4]);

            } else {
                usage();
            }
            break;
    }
    return 0;
    
}