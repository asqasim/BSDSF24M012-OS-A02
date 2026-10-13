/*
* Programming Assignment 02: ls v1.3.0
* Features:
*   v1.1.0: -l (long listing format)
*   v1.2.0: default multi-column display, "down then across"
*   v1.3.0: -x horizontal ("across") display
* Usage:
*       $ ls            -> column listing of "."
*       $ ls -l         -> long listing
*       $ ls -x         -> horizontal listing
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

/* Which display function should be used. Set once in main(). */
typedef enum
{
    MODE_DEFAULT,     /* down then across */
    MODE_LONG,        /* -l */
    MODE_HORIZONTAL   /* -x */
} display_mode_t;

extern int errno;

void do_ls(const char *dir, display_mode_t mode);
int read_entries(const char *dir, char ***names_out, size_t *count_out, size_t *maxlen_out);
void free_entries(char **names, size_t count);
void print_vertical(char **names, size_t count, size_t maxlen);
void print_horizontal(char **names, size_t count, size_t maxlen);
void print_long(const char *dir, char **names, size_t count);
void mode_to_string(mode_t mode, char *str);
int num_width(long long n);
int get_term_width(void);

int main(int argc, char *argv[])
{
    int opt;
    display_mode_t mode = MODE_DEFAULT;

    /* "lx": both -l and -x are valid options without values.
       If the user gives both, the last one wins. */
    while ((opt = getopt(argc, argv, "lx")) != -1)
    {
        switch (opt)
        {
        case 'l':
            mode = MODE_LONG;
            break;
        case 'x':
            mode = MODE_HORIZONTAL;
            break;
        default: /* '?' = unknown option */
            fprintf(stderr, "Usage: %s [-l | -x] [dir...]\n", argv[0]);
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
            printf("Directory listing of %s : \n", argv[i]);
            do_ls(argv[i], mode);
            puts("");
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

/* The main driver: read, then dispatch on the display mode */
void do_ls(const char *dir, display_mode_t mode)
{
    char **names = NULL;
    size_t count = 0, maxlen = 0;

    if (read_entries(dir, &names, &count, &maxlen) == -1)
        return;

    switch (mode)
    {
    case MODE_LONG:
        print_long(dir, names, count);
        break;
    case MODE_HORIZONTAL:
        print_horizontal(names, count, maxlen);
        break;
    default:
        print_vertical(names, count, maxlen);
        break;
    }

    free_entries(names, count);
}

/* Default display: columns, "down then across" */
void print_vertical(char **names, size_t count, size_t maxlen)
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
            if (idx + rows >= count)             /* last item in this row */
                printf("%s", names[idx]);
            else
                printf("%-*s", (int)col_width, names[idx]);
        }
        putchar('\n');
    }
}

/* -x display: left to right, wrap when the line is full */
void print_horizontal(char **names, size_t count, size_t maxlen)
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
        printf("%-*s", (int)col_width, names[i]);
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

/* -l display: works from the already-read array of names */
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

        printf("%s %*ld %-*s %-*s %*lld %s %s",
               mode_str,
               w_links, (long)st.st_nlink,
               w_user, ubuf,
               w_group, gbuf,
               w_size, (long long)st.st_size,
               timebuf,
               names[i]);

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
