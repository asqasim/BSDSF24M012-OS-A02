/*
* Programming Assignment 02: ls v1.1.0
* Feature: -l (long listing format)
* Usage:
*       $ ls            -> simple listing of "."
*       $ ls -l         -> long listing of "."
*       $ ls -l /etc /tmp
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

extern int errno;

void do_ls(const char *dir);
void do_ls_long(const char *dir);
void mode_to_string(mode_t mode, char *str);
int num_width(long long n);

int main(int argc, char *argv[])
{
    int opt;
    int long_flag = 0;

    /* getopt scans argv for options. "l" means -l is a valid option
       (with no value). It returns -1 when options are finished. */
    while ((opt = getopt(argc, argv, "l")) != -1)
    {
        switch (opt)
        {
        case 'l':
            long_flag = 1;
            break;
        default: /* '?' = unknown option */
            fprintf(stderr, "Usage: %s [-l] [dir...]\n", argv[0]);
            return 1;
        }
    }

    /* After getopt, optind is the index of the first non-option
       argument (the directory names). */
    if (optind == argc)
    {
        if (long_flag)
            do_ls_long(".");
        else
            do_ls(".");
    }
    else
    {
        for (int i = optind; i < argc; i++)
        {
            printf("Directory listing of %s : \n", argv[i]);
            if (long_flag)
                do_ls_long(argv[i]);
            else
                do_ls(argv[i]);
            puts("");
        }
    }
    return 0;
}

/* Original simple listing, unchanged from v1.0.0 */
void do_ls(const char *dir)
{
    struct dirent *entry;
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        printf("%s\n", entry->d_name);
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);
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

void do_ls_long(const char *dir)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }

    struct dirent *entry;
    struct stat st;
    char path[PATH_MAX];
    int w_links = 0, w_user = 0, w_group = 0, w_size = 0;
    long long total_blocks = 0;

    /* ---------- PASS 1: find column widths and the "total" ---------- */
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
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
    if (errno != 0)
        perror("readdir failed");

    /* ls reports total in 1K blocks, st_blocks is in 512-byte units */
    printf("total %lld\n", total_blocks / 2);

    /* Go back to the start of the directory for the second pass */
    rewinddir(dp);

    /* ---------- PASS 2: print every entry ---------- */
    time_t now = time(NULL);
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
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
               entry->d_name);

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

    closedir(dp);
}
