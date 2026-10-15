# Programming in SystemZero

`cc` compiles C-like source to a native ELF32 executable. `ld` links objects
and archives. Both commands run inside SystemZero and save files in the
current directory.

## Quick start

From the default `/home` workspace:

```text
cc examples/hello.sz hello
./hello
```

Create your own source with `touch greeting.sz` and `edit greeting.sz`:

```c
int main() {
    print("Hello from SystemZero!\n");
    return 0;
}
```

Save with Ctrl+S, exit with Escape, then run:

```text
cc greeting.sz greeting
./greeting
```

`PATH` defaults to `sys:/bin`. Local executables run as `./program`. Source and
output must differ. Compile/link failures report an error and retain the
output file. Compiler diagnostics include a source line and column.

| Example               | Demonstrates                    |
| --------------------- | ------------------------------- |
| `examples/hello.sz`   | Functions and console output    |
| `examples/files.sz`   | File creation and reading       |
| `examples/draw.sz`    | Text cells, pixels, and lines   |
| `examples/threads.sz` | Workers and mutexes             |
| `examples/sdk.sz`     | Header includes and SDK linking |

## Zedit

Run `zedit greeting.sz` to open or create source. The editor highlights keywords,
strings, comments, numbers, function names, variables, and include directives.
It uses the text editor's visual line wrapping, scrolling, and tab stops.
The left gutter shows source line numbers; wrapped continuations leave it blank.
Mouse placement and selection account for the gutter.

| Key                 | Action                                       |
| ------------------- | -------------------------------------------- |
| Ctrl+S              | Save                                         |
| Click / drag        | Place caret / select source text             |
| Ctrl+C / Ctrl+V     | Copy / paste, replacing selected text        |
| Mouse wheel         | Scroll without moving the caret              |
| F5                  | Save, compile, and run                       |
| Ctrl+Space          | Open completion                              |
| Up / Down           | Select a suggestion while completion is open |
| Tab / Enter         | Accept a suggestion while completion is open |
| Escape              | Dismiss completion; otherwise quit           |
| Arrows, Home, End   | Move through the source                      |
| Page Up / Page Down | Move one page                                |
| Ctrl+Home / Ctrl+R  | Start of file                                |
| Ctrl+End            | End of file                                  |

Completion opens while typing an identifier with at least two characters. It
lists visible variables and function names. The selected item shows a function
signature or variable declaration, plus its origin: local function, parameter,
variable, imported header path, or built-in kernel API. Imported declarations
use the same local-header and `INCLUDE` lookup as `cc`, including `-I`.
Copy preserves tabs and line breaks. The shared guest clipboard holds up to
64 KiB and remains available after quitting the editor.

The source index recognizes literal includes and declarations, and tolerates
incomplete source; compiler diagnostics determine whether the program builds.

F5 switches to a run console. A `.sz` file builds to the same path without that
extension; other names build to `NAME.run`. The console runs the new executable
only if compilation succeeds. Diagnostics and output stay visible until any
key is pressed, then the editor restores its source, cursor, and scroll position.
Programs can use interactive input and the graphics and thread APIs. Quitting
the editor restores the console that opened it. Escape discards unsaved changes.

`zedit` links `system.a` by default, covering SDK `sz_*` functions. To choose
other libraries or an include directory, pass the options after the source:

```text
zedit app.sz io.o -I headers
zedit app.sz system.a helper.o
```

Those arguments replace the default `system.a`. Built-ins require no linked
object. `zedit` edits sources up to 64 KiB and indexes up to 512 symbols and
32 header paths, with an include depth of eight. Hitting an index limit is
reported in the footer.

## Built-ins and linked functions

All functions in the API tables below are built-ins. Their names are reserved;
the compiler emits calls through the running kernel's service table. They need
no header or library argument.

| Function form | Declaration             | Link input |
| ------------- | ----------------------- | ---------- |
| `print`       | Built into the compiler | None       |
| `sz_print`    | `<systemzero/io.h>`      | `io.o`     |

`print` and `sz_print` call the same kernel operation. SDK objects provide
explicit wrappers. See [libraries and linking](#libraries-and-linking).

## Language syntax

### Types and functions

| Type    | Representation        |
| ------- | --------------------- |
| `int`   | Signed 32-bit integer |
| `char`  | Unsigned 8-bit byte   |
| `void`  | No value              |
| Pointer | 32-bit address        |

The entry is `int main()`, `int main(void)`, or `int main(char *args)`.
`args` contains the complete command line. Returning from `main` sets the
program's exit status; falling off a function returns zero.

Functions support prototypes, recursion, and up to eight scalar/pointer
arguments. Arguments are evaluated left to right. Variables may be local or
global; declarations can contain multiple names. Blocks introduce local scopes.

```c
int count = 0;
char name[] = "example";

int square(int n) {
    return n * n;
}
```

### Arrays and pointers

One-dimensional arrays use literal lengths. Arrays decay to pointers in
expressions. `&` takes an address, `*` dereferences, and pointer arithmetic
scales by the element size.

```c
int values[4];
int *p = values;
p[0] = 10;
*(p + 1) = 20;
```

Local scalar variables start at zero. Initialize local arrays with assignments
or `memset`. Global scalars accept literal initializers; global character
arrays accept strings. Cast a `void*` to a typed pointer before dereferencing
or doing pointer arithmetic.

### Statements and operators

Statements include blocks, expressions, `if/else`, `while`, `break`,
`continue`, and `return`. Operators follow C precedence.

| Category   | Operators                                        |
| ---------- | ------------------------------------------------ |
| Arithmetic | `+ - * / %`, unary `+ -`, prefix/postfix `++ --` |
| Comparison | `== != < <= > >=`                                |
| Bitwise    | `& \| ^ ~ << >>`                                 |
| Boolean    | `! && \|\|`                                      |
| Assignment | `= += -= *= /= %=`                               |
| Pointers   | `&value`, `*pointer`, `pointer[index]`           |
| Casts      | `(int)value`, `(char*)pointer`                   |
| Size       | `sizeof(int)`, `sizeof(char*)`                   |

Boolean operations short-circuit and produce 0 or 1. Integer arithmetic keeps
32 bits; division truncates toward zero and integer right shifts are arithmetic.
Characters keep the low eight bits.

Numbers may be decimal or hexadecimal. Strings/characters support `\n`, `\r`,
`\t`, `\0`, `\\`, `\"`, `\'`, and `\xHH`. Comments use `//` or `/* ... */`.

Unsupported: floating point, structures, enums, multidimensional arrays,
`for`, `switch`, variadic functions, and general function-pointer declarations.
A worker function name can be passed to `thread_start` or stored in a `void*`.

## Kernel API

Parameters without a type annotation are `int`. Strings use NUL-terminated
`char*`; memory buffers use `void*`. Results are `int` unless a pointer is shown.
Text coordinates are zero-based rows/columns; graphics coordinates are pixels.
File lengths, memory lengths, and file offsets are bytes. Supply buffers large
enough for each operation.

### Console input and output

| Function    | Parameters       | Result                     |
| ----------- | ---------------- | -------------------------- |
| `print`     | `char *text`     | 0; -1 for null text        |
| `print_dec` | `value`          | 0; prints signed decimal   |
| `print_hex` | `value`          | 0; prints hexadecimal      |
| `putchar`   | `ch`             | Printed byte               |
| `newline`   | None             | 0                          |
| `readline`  | `char *buf, cap` | Length; -1 on Escape/error |
| `getchar`   | None             | Character code             |

`readline` capacity includes the NUL terminator and must be 2-65,536 bytes.
`getchar` does not echo; Enter is 10 and Escape is 27. Input is foreground-only.

### Text display

| Function         | Parameters                   | Result                    |
| ---------------- | ---------------------------- | ------------------------- |
| `clear`          | None                         | 0                         |
| `set_color`      | `attr`                       | 0; -1 for invalid attr    |
| `get_color`      | None                         | Current drawing attribute |
| `screen_rows`    | None                         | Text grid height          |
| `screen_columns` | None                         | Text grid width           |
| `put_text`       | `row, col, char *text, attr` | 0; -1 on invalid input    |
| `put_cell`       | `row, col, ch, attr`         | 0; -1 on invalid input    |
| `begin_update`   | None                         | 0; begin batched drawing  |
| `end_update`     | None                         | 0; present batched cells  |

`attr = background * 16 + foreground`, with palette indices 0-15.
`set_color` changes the program's drawing attribute. Persistent colors use
`env_set` or the settings services. The text grid is 96x48 in graphics mode
and 80x24 in text mode. Clearing preserves the status bar.

Batch several positioned text changes to present them together:

```c
begin_update();
put_text(2, 2, "Title", 31);
put_cell(3, 2, '*', 31);
end_update();
```

### Graphics

| Function     | Parameters              | Result                 |
| ------------ | ----------------------- | ---------------------- |
| `draw_pixel` | `x, y, color`           | 0; -1 on invalid input |
| `draw_line`  | `x1, y1, x2, y2, color` | 0; -1 on invalid input |

Colors are palette indices 0-15. Graphics require a framebuffer. Its 12-pixel
status-bar margin is reserved; drawing into it returns -1.

### Memory and strings

| Function | Parameters                    | Result                          |
| -------- | ----------------------------- | ------------------------------- |
| `alloc`  | `bytes`                       | `void*`; 0 on failure           |
| `memcpy` | `void *dst, void *src, bytes` | `void*` destination; 0 on error |
| `memset` | `void *dst, byte, bytes`      | `void*` destination; 0 on error |
| `strlen` | `char *text`                  | Length; -1 for null text        |

Command allocations are reclaimed when the command returns. Workers share
that heap with their parent command.

### Files and directories

| Function      | Parameters                      | Result                   |
| ------------- | ------------------------------- | ------------------------ |
| `file_size`   | `char *path`                    | Size; -1 if unavailable  |
| `file_read`   | `char *path, void *buf, n, off` | Bytes read; -1 on error  |
| `file_write`  | `char *path, void *buf, n`      | 0; -1 on failure         |
| `file_delete` | `char *path`                    | 0; -1 on failure         |
| `file_create` | `char *path`                    | 0; -1 if not created     |
| `mkdir`       | `char *path`                    | 0; negative ESFS error   |
| `chdir`       | `char *path`                    | 0; -1 on failure         |
| `getcwd`      | `char *buf, cap`                | Path length; -1 on error |

`file_read` returns 0 at EOF. `file_write` creates or replaces a file;
`file_create` requires an absent file. `file_delete` accepts files and empty
directories. Paths may be relative, absolute, or qualified, such as
`sys:/home/app.sz` and `1p1:/source/app.sz`. `chdir` is foreground-only.

### Commands and threads

| Function       | Parameters         | Result                             |
| -------------- | ------------------ | ---------------------------------- |
| `execute`      | `char *command`    | Command exit status                |
| `sleep`        | `milliseconds`     | 0; -1 on invalid duration          |
| `thread_start` | `void *entry, arg` | Positive thread ID; -1 on error    |
| `thread_join`  | `id`               | Worker result; -1 on error         |
| `thread_yield` | None               | 0                                  |
| `thread_sleep` | `milliseconds`     | 0; -1 on invalid duration          |
| `thread_id`    | None               | Current ID; foreground is 0        |
| `thread_count` | None               | Number of running/sleeping workers |
| `mutex_create` | None               | Positive handle; -1 on error       |
| `mutex_lock`   | `id`               | 0; -1 on invalid/recursive lock    |
| `mutex_unlock` | `id`               | 0; -1 if caller is not the owner   |

Sleep durations are 0-60,000 ms; zero yields. `execute` is foreground-only and
returns 2 if a command cannot be executed.

### Environment and settings

| Function                | Parameters                  | Result                    |
| ----------------------- | --------------------------- | ------------------------- |
| `env_get`               | `char *key, char *buf, cap` | Value length; -1 on error |
| `env_set`               | `char *key, char *value`    | 0; -1 on failure          |
| `env_apply`             | None                        | 0; -1 from a worker       |
| `clock_time`            | None                        | Seconds since midnight    |
| `status_position`       | None                        | 0 top; 1 bottom           |
| `settings_apply`        | `sec, pos, attr`            | 0; -1 on failure          |
| `settings_apply_colors` | `sec, pos, attr, secondary` | 0; -1 on failure          |

`env_set` validates, saves, and applies a value. `env_apply` reapplies display
settings. `settings_apply` saves clock/bar/colors and keeps the secondary color
visible; `settings_apply_colors` sets it explicitly. `sec` is 0-86,399, `pos` is
0 or 1, and `secondary` is 0-15. Both text colors must differ from the background.
These mutation services are foreground-only. See
[environment variables](storage.md#environment-variables) for names/defaults.

### Disk operations

| Function        | Parameters        | Result           |
| --------------- | ----------------- | ---------------- |
| `disk_edit`     | `disk, action, n` | 0; -1 on failure |
| `volume_format` | `disk, partition` | 0; -1 on failure |

Both operations are foreground-only. The running system disk's table, EFI
partition, and system volume are protected. Formatting removes all volume files.

| `disk_edit` action | `n`                           |
| ------------------ | ----------------------------- |
| `0`                | Table type: 1 MBR, 2 GPT      |
| `1`                | New partition size in sectors |
| `2`                | Partition slot to delete      |

A sector is 512 bytes. Creating a table replaces its partition layout; deleting
a partition removes access to its files. Use `diskman` for confirmed GUI changes.

### Palette

| Index | Color   | Index | Color         |
| ----: | ------- | ----: | ------------- |
| 0     | Black   | 8     | Gray          |
| 1     | Blue    | 9     | Light blue    |
| 2     | Green   | 10    | Light green   |
| 3     | Cyan    | 11    | Light cyan    |
| 4     | Red     | 12    | Light red     |
| 5     | Magenta | 13    | Light magenta |
| 6     | Brown   | 14    | Yellow        |
| 7     | White   | 15    | Bright white  |

## Threads

Workers share code, globals, heap, filesystem, and console on one CPU. Each
has a 64 KiB stack; up to 13 workers can hold handles. Scheduling uses a 5 ms
quantum. There is no process isolation, SMP, or floating-point context support.

```c
int worker(int n) {
    thread_sleep(100);
    return n * 2;
}

int main() {
    int id = thread_start(worker, 21);
    if (id < 0) return 1;
    print_dec(thread_join(id));
    newline();
    return 0;
}
```

Workers take one `int` and return `int`. Use mutexes around shared updates such
as `total++` or a sequence of console calls. API operations are serialized.
Mutexes are nonrecursive and command-owned. Joining consumes the worker handle.
The command waits for its workers before unloading and reclaiming memory.

## Libraries and linking

### SDK

Headers are searched through `INCLUDE=sys:/sys/include`. Objects/archives are
searched through `LIBRARY_PATH=sys:/sys/lib`, after the supplied path. Quoted
includes check the source's directory first; `-I DIRECTORY` adds one directory.

| Header                | Object     | Services                    |
| --------------------- | ---------- | --------------------------- |
| `systemzero/io.h`     | `io.o`     | Console, text, graphics     |
| `systemzero/memory.h` | `memory.o` | Memory and strings          |
| `systemzero/fs.h`     | `fs.o`     | Files and directories       |
| `systemzero/thread.h` | `thread.o` | Threads, mutexes, execution |
| `systemzero/env.h`    | `env.o`    | Environment, clock, disks   |
| `systemzero/system.h` | `system.a` | All SDK groups              |

SDK names use the `sz_` prefix:

```c
#include <systemzero/io.h>

int main() {
    sz_print("Linked SDK call\n");
    return 0;
}
```

```text
cc app.sz app io.o
./app
```

`system.a` contains all SDK groups. Link the archive or individual group objects;
using both defines the same symbols twice.

### Separate compilation

Declare external functions with prototypes or headers, then compile/link:

```text
cc -c helper.sz helper.o
cc -c main.sz main.o
ld app main.o helper.o system.a
./app
```

`cc SOURCE OUTPUT OBJECT...` performs compilation and linking in one command.
The preprocessor supports includes, object-like `#define`, and
`#ifdef`/`#ifndef` with `#else`/`#endif`.

## Limits and executable format

| Resource                    | Limit                      |
| --------------------------- | -------------------------- |
| Expanded source             | 64 KiB                     |
| Tokens                      | 16,384                     |
| Functions / globals         | 128 each                   |
| Visible locals              | 256                        |
| Local storage per function  | 16 KiB                     |
| Arguments per user function | 8                          |
| Strings and global data     | 64 KiB combined            |
| Preprocessor macros         | 64                         |
| Include nesting             | 8 levels                   |
| Compiler object output      | 256 KiB                    |
| Linker objects / symbols    | 64 / 4,096                 |
| Executable file             | 916,992 bytes (ESFS limit) |

Objects are ELF32 `ET_REL`; executables are position-independent ELF32 `ET_DYN`.
The linker supports i386 absolute, PC-relative, PLT, GOT, GOTOFF relocations,
BSS, and ar archives. It links all archive members. COMMON symbols, TLS,
shared-library imports, and other architectures are unsupported.

The entry checks the kernel API table before calling `main`. Built-ins use
`shared/sys_api.def`; SDK wrappers use the same service slots. The native call
interface is i386 cdecl with a hidden environment pointer and five arguments.
The loader validates segments, applies relocations, and restores the command's
heap and console update/cursor state when it returns.
