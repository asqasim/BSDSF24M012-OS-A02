/*
* Programming Assignment 02: ls v1.6.0
* Features:
*   v1.1.0: -l (long listing format)
*   v1.2.0: default multi-column display, "down then across"
*   v1.3.0: -x horizontal ("across") display
*   v1.4.0: alphabetical sorting of all output
*   v1.5.0: colorized output based on file type
*   v1.6.0: -R recursive listing
* Usage:
*       $ ls            -> sorted, colored column listing of "."
*       $ ls -l         -> sorted, colored long listing
*       $ ls -x         -> sorted, colored horizontal listing
*       $ ls -R         -> recursive listing (combine with -l or -x)
*       $ ls -x /etc /tmp
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

/* ANSI escape codes: ESC [ <codes> m  */
#define COLOR_RESET   "\033[0m"
#define COLOR_BLUE    "\033[0;34m"   /* directories */
#define COLOR_GREEN   "\033[0;32m"   /* executables */
#define COLOR_RED     "\033[0;31m"   /* archives */
#define COLOR_PINK    "\033[1;35m"   /* symbolic links */
#define COLOR_REVERSE "\033[7m"      /* special files */

/* Which display function should be used. Set once in main(). */
typedef enum
{
    MODE_DEFAULT,     /* down then across */
    MODE_LONG,        /* -l */
    MODE_HORIZONTAL   /* -x */
} display_mode_t;

extern int errno;

/* 1 if stdout is a terminal (colors on), 0 if piped/redirected (colors off) */
int use_color = 0;

/* 1 if -R was given: descend into subdirectories */
int recursive_flag = 0;

void do_ls(const char *dir, display_mode_t mode);
int read_entries(const char *dir, char ***names_out, size_t *count_out, size_t *maxlen_out);
void free_entries(char **names, size_t count);
int cmp_names(const void *a, const void *b);
int join_path(char *out, size_t size, const char *dir, const char *name);
int has_suffix(const char *name, const char *suffix);
const char *color_for(const char *name, mode_t mode);
void print_colored(const char *name, mode_t mode);
mode_t get_mode(const char *dir, const char *name);
void print_vertical(const char *dir, char **names, size_t count, size_t maxlen);
void print_horizontal(const char *dir, char **names, size_t count, size_t maxlen);
void print_long(const char *dir, char **names, size_t count);
void mode_to_string(mode_t mode, char *str);
int num_width(long long n);
int get_term_width(void);

int main(int argc, char *argv[])
{
    int opt;
    display_mode_t mode = MODE_DEFAULT;

    /* Only use colors when output goes to a real terminal.
       Otherwise "ls | cat" or "ls > file" would be full of escape codes. */
    use_color = isatty(STDOUT_FILENO);

    /* "lxR": all are valid options without values.
       -l and -x choose the display mode (last one wins);
       -R is independent and turns on recursion. */
    while ((opt = getopt(argc, argv, "lxR")) != -1)
    {
        switch (opt)
        {
        case 'l':
            mode = MODE_LONG;
            break;
        case 'x':
            mode = MODE_HORIZONTAL;
            break;
        case 'R':
            recursive_flag = 1;
            break;
        default: /* '?' = unknown option */
            fprintf(stderr, "Usage: %s [-l | -x] [-R] [dir...]\n", argv[0]);
            return 1;
        }
    }

    if (optind == argc)
    {
        do_ls(".", mode);
    }
    else
    {
        for (int i = optind; i < argc; i++)
        {
            if (recursive_flag)
            {
                /* do_ls prints the "dir:" header itself; just separate args */
                if (i > optind)
                    puts("");
                do_ls(argv[i], mode);
            }
            else
            {
                printf("Directory listing of %s : \n", argv[i]);
                do_ls(argv[i], mode);
                puts("");
            }
        }
    }
    return 0;
}

/* Ask the terminal how many columns wide it is.
   Falls back to 80 if stdout is not a terminal (e.g. piped). */
int get_term_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return 80;
    return ws.ws_col;
}

/* Read all non-hidden names of a directory into a dynamic array.
   On success: *names_out = array, *count_out = how many, *maxlen_out = longest name.
   Returns 0 on success, -1 if the directory cannot be opened. */
int read_entries(const char *dir, char ***names_out, size_t *count_out, size_t *maxlen_out)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return -1;
    }

    char **names = NULL;
    size_t count = 0, capacity = 0, maxlen = 0;
    struct dirent *entry;

    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        if (count == capacity)  /* array full: double its size */
        {
            capacity = capacity ? capacity * 2 : 16;
            char **tmp = realloc(names, capacity * sizeof(char *));
            if (tmp == NULL)
            {
                perror("realloc");
                break;
            }
            names = tmp;
        }

        names[count] = strdup(entry->d_name);  /* own copy of the name */
        if (names[count] == NULL)
        {
            perror("strdup");
            break;
        }
        size_t len = strlen(names[count]);
        if (len > maxlen)
            maxlen = len;
        count++;
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);

    *names_out = names;
    *count_out = count;
    *maxlen_out = maxlen;
    return 0;
}

void free_entries(char **names, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* Comparison function for qsort (each argument is a char** into the array) */
int cmp_names(const void *a, const void *b)
{
    const char *s1 = *(const char * const *)a;
    const char *s2 = *(const char * const *)b;
    return strcmp(s1, s2);
}

/* Build "dir/name" into out. Avoids a double slash when dir already ends
   in '/' (e.g. "src/" + "x" -> "src/x", not "src//x").
   Returns 0 on success, -1 if the result would not fit. */
int join_path(char *out, size_t size, const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    int n;
    if (dlen > 0 && dir[dlen - 1] == '/')
        n = snprintf(out, size, "%s%s", dir, name);
    else
        n = snprintf(out, size, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= size)
        return -1;
    return 0;
}

/* The main driver: read, sort, display, then (with -R) recurse */
void do_ls(const char *dir, display_mode_t mode)
{
    char **names = NULL;
    size_t count = 0, maxlen = 0;

    /* -R: every directory is announced with a header, like real ls */
    if (recursive_flag)
        printf("%s:\n", dir);

    if (read_entries(dir, &names, &count, &maxlen) == -1)
        return;

    if (count > 1)
        qsort(names, count, sizeof(char *), cmp_names);

    switch (mode)
    {
    case MODE_LONG:
        print_long(dir, names, count);
        break;
    case MODE_HORIZONTAL:
        print_horizontal(dir, names, count, maxlen);
        break;
    default:
        print_vertical(dir, names, count, maxlen);
        break;
    }

    /* Recursion: this directory is fully printed, now visit its subdirectories.
       lstat (not stat) so a symlink to a directory is NOT followed; this
       prevents infinite loops through symlink cycles.
       Base case: a directory with no subdirectories -> the loop never calls
       do_ls again, so the recursion unwinds. */
    if (recursive_flag)
    {
        for (size_t i = 0; i < count; i++)
        {
            char path[PATH_MAX];
            struct stat st;

            if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0)
                continue;
            if (join_path(path, sizeof(path), dir, names[i]) == -1)
            {
                fprintf(stderr, "Path too long : %s/%s\n", dir, names[i]);
                continue;
            }
            if (lstat(path, &st) == -1)
            {
                perror(path);
                continue;
            }
            if (S_ISDIR(st.st_mode))
            {
                putchar('\n');          /* blank line before next header */
                do_ls(path, mode);      /* full path, not just the name */
            }
        }
    }

    free_entries(names, count);
}

/* ------------------------- color helpers ------------------------- */

/* Does name end with suffix? (e.g. "a.tar" ends with ".tar") */
int has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

/* Decide the color from the file's st_mode (from lstat) and its name.
   Order matters: a symlink must be checked before anything else, and a
   directory before the execute check (directories have x bits too). */
const char *color_for(const char *name, mode_t mode)
{
    if (S_ISLNK(mode))
        return COLOR_PINK;
    if (S_ISDIR(mode))
        return COLOR_BLUE;
    if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) || S_ISSOCK(mode))
        return COLOR_REVERSE;
    if (has_suffix(name, ".tar") || has_suffix(name, ".gz") || has_suffix(name, ".zip"))
        return COLOR_RED;
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return COLOR_GREEN;
    return "";   /* ordinary file: default color */
}

/* Print a filename in its color (no padding, no newline) */
void print_colored(const char *name, mode_t mode)
{
    const char *color = use_color ? color_for(name, mode) : "";
    if (color[0] != '\0')
        printf("%s%s%s", color, name, COLOR_RESET);
    else
        printf("%s", name);
}

/* Get st_mode of dir/name using lstat. Returns 0 if lstat fails. */
mode_t get_mode(const char *dir, const char *name)
{
    char path[PATH_MAX];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (lstat(path, &st) == -1)
        return 0;
    return st.st_mode;
}

/* ------------------------- display functions ------------------------- */

/* Default display: columns, "down then across".
   NOTE: we cannot use printf("%-*s") to pad a colored name, because the
   invisible escape characters would count toward the width. So we print
   the colored name first and then add the padding spaces ourselves. */
void print_vertical(const char *dir, char **names, size_t count, size_t maxlen)
{
    if (count == 0)
        return;

    size_t col_width = maxlen + 2;               /* name + 2 spaces */
    size_t cols = (size_t)get_term_width() / col_width;
    if (cols < 1) cols = 1;                      /* name wider than terminal */
    if (cols > count) cols = count;
    size_t rows = (count + cols - 1) / cols;     /* ceiling division */

    for (size_t r = 0; r < rows; r++)
    {
        for (size_t c = 0; c < cols; c++)
        {
            size_t idx = r + c * rows;           /* down-then-across index */
            if (idx >= count)
                break;

            print_colored(names[idx], get_mode(dir, names[idx]));

            if (idx + rows < count)              /* another column follows */
                printf("%*s", (int)(col_width - strlen(names[idx])), "");
        }
        putchar('\n');
    }
}

/* -x display: left to right, wrap when the line is full */
void print_horizontal(const char *dir, char **names, size_t count, size_t maxlen)
{
    if (count == 0)
        return;

    size_t col_width = maxlen + 2;
    size_t term_width = (size_t)get_term_width();
    size_t pos = 0;                              /* current horizontal position */

    for (size_t i = 0; i < count; i++)
    {
        /* would this item go past the right edge? then start a new line */
        if (pos > 0 && pos + col_width > term_width)
        {
            putchar('\n');
            pos = 0;
        }
        print_colored(names[i], get_mode(dir, names[i]));
        printf("%*s", (int)(col_width - strlen(names[i])), "");
        pos += col_width;
    }
    putchar('\n');
}

/* Number of digits needed to print n (used for column alignment) */
int num_width(long long n)
{
    char buf[32];
    return snprintf(buf, sizeof(buf), "%lld", n);
}

/* Convert st_mode into a string like "drwxr-xr-x" (needs 11 bytes) */
void mode_to_string(mode_t mode, char *str)
{
    /* 1st character: file type */
    if (S_ISDIR(mode))       str[0] = 'd';
    else if (S_ISLNK(mode))  str[0] = 'l';
    else if (S_ISCHR(mode))  str[0] = 'c';
    else if (S_ISBLK(mode))  str[0] = 'b';
    else if (S_ISFIFO(mode)) str[0] = 'p';
    else if (S_ISSOCK(mode)) str[0] = 's';
    else                     str[0] = '-';

    /* Next 9 characters: rwx for owner, group, others */
    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IXUSR) ? 'x' : '-';
    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IXGRP) ? 'x' : '-';
    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IXOTH) ? 'x' : '-';
    str[10] = '\0';

    /* Special bits replace the 'x' position:
       setuid -> 's' (or 'S' if no x), setgid -> same, sticky -> 't'/'T' */
    if (mode & S_ISUID)
        str[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID)
        str[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX)
        str[9] = (mode & S_IXOTH) ? 't' : 'T';
}

/* -l display: works from the already-read (and sorted) array of names */
void print_long(const char *dir, char **names, size_t count)
{
    struct stat st;
    char path[PATH_MAX];
    int w_links = 0, w_user = 0, w_group = 0, w_size = 0;
    long long total_blocks = 0;

    /* ---------- PASS 1: find column widths and the "total" ---------- */
    for (size_t i = 0; i < count; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (lstat(path, &st) == -1)
        {
            perror(path);
            continue;
        }

        struct passwd *pw = getpwuid(st.st_uid);
        struct group *gr = getgrgid(st.st_gid);
        int ulen = pw ? (int)strlen(pw->pw_name) : num_width(st.st_uid);
        int glen = gr ? (int)strlen(gr->gr_name) : num_width(st.st_gid);

        if (num_width(st.st_nlink) > w_links) w_links = num_width(st.st_nlink);
        if (ulen > w_user)  w_user = ulen;
        if (glen > w_group) w_group = glen;
        if (num_width(st.st_size) > w_size) w_size = num_width(st.st_size);

        total_blocks += st.st_blocks; /* counted in 512-byte units */
    }

    /* ls reports total in 1K blocks, st_blocks is in 512-byte units */
    printf("total %lld\n", total_blocks / 2);

    /* ---------- PASS 2: print every entry ---------- */
    time_t now = time(NULL);
    for (size_t i = 0; i < count; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (lstat(path, &st) == -1)
            continue;

        char mode_str[11];
        mode_to_string(st.st_mode, mode_str);

        struct passwd *pw = getpwuid(st.st_uid);
        struct group *gr = getgrgid(st.st_gid);
        char ubuf[32], gbuf[32];
        if (pw) snprintf(ubuf, sizeof(ubuf), "%s", pw->pw_name);
        else    snprintf(ubuf, sizeof(ubuf), "%u", st.st_uid);
        if (gr) snprintf(gbuf, sizeof(gbuf), "%s", gr->gr_name);
        else    snprintf(gbuf, sizeof(gbuf), "%u", st.st_gid);

        /* ctime() gives "Wed Jun 30 21:49:08 1993\n"
           chars 4..15 = "Jun 30 21:49", chars 20..23 = year.
           Like real ls: files older than ~6 months show the year. */
        time_t mtime = st.st_mtime;
        char *t = ctime(&mtime);
        char timebuf[32];
        if (now - mtime > 15552000 || mtime > now)
            snprintf(timebuf, sizeof(timebuf), "%.6s  %.4s", t + 4, t + 20);
        else
            snprintf(timebuf, sizeof(timebuf), "%.12s", t + 4);

        /* everything before the filename */
        printf("%s %*ld %-*s %-*s %*lld %s ",
               mode_str,
               w_links, (long)st.st_nlink,
               w_user, ubuf,
               w_group, gbuf,
               w_size, (long long)st.st_size,
               timebuf);

        /* the filename, colored (we already have st_mode from lstat) */
        print_colored(names[i], st.st_mode);

        /* For symbolic links, ls -l shows "name -> target" */
        if (S_ISLNK(st.st_mode))
        {
            char target[PATH_MAX];
            ssize_t len = readlink(path, target, sizeof(target) - 1);
            if (len != -1)
            {
                target[len] = '\0';
                printf(" -> %s", target);
            }
        }
        putchar('\n');
    }
}
