# Operating Systems - Programming Assignment 02

---

## Feature 2: ls-v1.1.0 - Long Listing Format (-l)

### Q1. What is the difference between stat() and lstat()? When is lstat() better in ls?

Both system calls give information about a file (size, owner, permissions, time and so on). The difference is how they treat a symbolic link.

- `stat()` follows the link. It gives information about the file the link points to.
- `lstat()` does not follow the link. It gives information about the link itself.

In `ls`, `lstat()` is the better choice, because:

- `ls -l` must show a symbolic link as a link. It must print `l` as the file type and show `name -> target`. With `stat()` we would see the target file and lose this information.
- For colors, we must know that a file is a link so we can print it in pink.
- For `-R`, `lstat()` stops us from following a link to a directory. This avoids infinite loops when links point back to a parent directory.

In my code I use `lstat()` everywhere.

### Q2. How do bitwise operators and macros extract information from st_mode?

`st_mode` is one integer. The upper bits store the file type and the lower bits store the permissions.

**File type.** We use the mask `S_IFMT` to keep only the type bits, then compare with a constant such as `S_IFDIR`:

```c
if ((st.st_mode & S_IFMT) == S_IFDIR)
    printf("directory");
```

The macros `S_ISDIR(mode)`, `S_ISREG(mode)`, `S_ISLNK(mode)` and others do the same thing in a shorter way.

**Permissions.** We use `&` with a permission constant. If the result is not zero, the bit is set:

```c
if (st.st_mode & S_IRUSR)   /* owner can read */
    str[1] = 'r';
```

The constants are:

- Owner: `S_IRUSR`, `S_IWUSR`, `S_IXUSR`
- Group: `S_IRGRP`, `S_IWGRP`, `S_IXGRP`
- Others: `S_IROTH`, `S_IWOTH`, `S_IXOTH`

**Special bits.** `S_ISUID`, `S_ISGID` and `S_ISVTX` (setuid, setgid, sticky). In the string they replace the `x` position with `s`, `S`, `t` or `T`.

In my `mode_to_string()` function I use these checks to build a string like `drwxr-xr-x`.

---

## Feature 3: ls-v1.2.0 - Column Display (Down Then Across)

### Q1. What is the logic for "down then across" printing? Why is one simple loop not enough?

First I read all names into an array and find the longest name. Then I calculate:

- `col_width = maxlen + 2` (the longest name plus 2 spaces)
- `cols = terminal_width / col_width`
- `rows = ceil(count / cols)`

Then I print row by row. For row `r` and column `c`, the file index is:

```
index = r + c * rows
```

For example, with 10 files and 3 rows, the first row prints files 0, 3, 6 and 9. The second row prints files 1, 4, 7 and so on. So the names go down each column first and then continue in the next column.

A simple single loop from 0 to count-1 is not enough because a terminal prints one line at a time from left to right. We cannot go back up to write in the next column. A single loop would print the names across the line (like `-x`), so the order would be wrong. We must jump through the array by `rows` to pick the right name for each position in the line.

### Q2. What is ioctl used for? What are the limits of a fixed 80 column width?

`ioctl()` with the `TIOCGWINSZ` request asks the terminal for its current size. It fills a `struct winsize`, and `ws_col` gives the number of columns. With this, the program knows how many columns it can fit.

If we only used a fixed width of 80:

- On a wide terminal, we would waste space and show fewer columns than we could.
- On a narrow terminal (less than 80), the lines would be too long and wrap in a broken way.
- If the user resizes the window, the layout would not adapt.

So the fixed width is only a fallback, for example when output is piped to a file.

---

## Feature 4: ls-v1.3.0 - Horizontal Display (-x)

### Q1. Compare the vertical and horizontal printing logic. Which needs more pre-calculation?

The horizontal ("across") logic is simpler. We loop through the array in order, print each name with padding, and keep a counter of the current position on the line. If the next name does not fit, we print a newline and reset the counter. We only need the terminal width and the column width.

The vertical ("down then across") logic is harder. Before printing, we must calculate the number of columns, then the number of rows, and then use the formula `r + c * rows` to find each name. We also have to handle the last column, which may have fewer items. So the vertical logic needs more pre-calculation.

### Q2. How did you manage the display modes? How does the program choose a function?

I use an enum called `display_mode_t` with three values:

- `MODE_DEFAULT` (down then across)
- `MODE_LONG` (`-l`)
- `MODE_HORIZONTAL` (`-x`)

In `main()`, the `getopt()` loop sets a `mode` variable when it sees `-l` or `-x`. If the user gives both, the last one wins. The mode is passed to `do_ls()`. After `do_ls()` reads and sorts the names, it uses a `switch` on the mode:

- `MODE_LONG` calls `print_long()`
- `MODE_HORIZONTAL` calls `print_horizontal()`
- default calls `print_vertical()`

The `-R` option is a separate flag (`recursive_flag`), so it can be combined with any display mode.

---

## Feature 5: ls-v1.4.0 - Alphabetical Sort

### Q1. Why must we read all entries before sorting? What are the drawbacks for huge directories?

`readdir()` returns entries in no special order. The name that should come first alphabetically could be the last one that we read. We cannot know the correct order until we have seen every name. So we must store all names in memory first, then sort them.

Drawbacks for a directory with millions of files:

- It uses a lot of memory (one pointer and one copied string per file).
- Sorting takes time (about n log n comparisons).
- The user sees no output until everything is read and sorted.
- If memory runs out, the program can fail.

### Q2. What is the purpose and signature of the qsort() comparison function? Why const void *?

`qsort()` is a general function. It does not know what type of data it is sorting, so it needs a function that tells it which of two elements comes first.

The signature is:

```c
int cmp_names(const void *a, const void *b);
```

It must return:

- a negative number if `a` should come before `b`
- zero if they are equal
- a positive number if `a` should come after `b`

Inside the function, we cast the pointers back to the real type. My array holds `char *`, so each argument is a pointer to a `char *`:

```c
const char *s1 = *(const char * const *)a;
const char *s2 = *(const char * const *)b;
return strcmp(s1, s2);
```

The arguments are `const void *` for two reasons. First, `void *` lets `qsort()` work with any data type. Second, `const` promises that the comparison function will not change the data.

---

## Feature 6: ls-v1.5.0 - Colorized Output

### Q1. How do ANSI escape codes make colors? What is the code for green?

ANSI escape codes are special characters that the terminal reads as commands instead of text. A code starts with the escape character (`\033`), then `[`, then the code numbers, then `m`. The terminal does not print them. It uses them to change the text color.

To print green text, we print the green code, then the text, then the reset code:

```c
printf("\033[0;32m%s\033[0m", "hello");
```

- `\033[0;32m` turns on green
- `\033[0m` resets to the normal color

Without the reset, all later text would also stay green. In my code, colors are only used when output goes to a real terminal (`isatty()`), so that files and pipes do not get extra escape characters.

### Q2. Which bits of st_mode show that a file is executable?

There are three execute bits:

- `S_IXUSR` for the owner
- `S_IXGRP` for the group
- `S_IXOTH` for others

To check if a file is executable by anyone:

```c
if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
    /* executable */
```

To check one class only, use just that bit, for example `mode & S_IXUSR` for the owner.

Directories also have execute bits (it means "can enter the directory"). So in my code I check for directories first and executables after that.

---

## Feature 7: ls-v1.6.0 - Recursive Listing (-R)

### Q1. What is a base case? What is the base case in your recursive ls?

A base case is the condition where a recursive function stops calling itself and just returns. Without it, the recursion would never end.

In my `do_ls()`, the base case is a directory that has no subdirectories (including an empty directory). The loop finds no entry that is a directory, so there are no more recursive calls, and the function returns to the parent call. Also, a directory that cannot be opened returns immediately. Because I use `lstat()`, symbolic links to directories are not followed, so the recursion cannot loop forever through a link cycle.

### Q2. Why do we need the full path before the recursive call? What if we call do_ls("subdir")?

The name from `readdir()` is only the name inside its parent, like `subdir`. It does not say where the parent is. `opendir()` and `lstat()` look for relative paths starting from the current working directory of the program, not from the parent directory.

If we called `do_ls("subdir")` from inside `do_ls("parent_dir")`, the program would look for `subdir` in the current directory. It would either fail with "Cannot open directory", or worse, list a different directory that happens to have the same name.

So I build the full path (`parent_dir/subdir`) with the `join_path()` function, and use it for both `lstat()` and the recursive call. This way, each level finds the correct directory.

---

## Conclusion

In this assignment I started with a simple ls program and added long listing, column display, horizontal display, sorting, colors, and recursion. I used one base source file for all features, and used Git branches, tags and releases for each version.
