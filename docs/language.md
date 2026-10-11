# Programming inside SystemZero

`cc` compiles a small C-like language into native, position-independent ELF32
executables. Compilation, saving, and execution all happen inside SystemZero.
The compiler does not invoke GCC, an external assembler, or an interpreter.

Four source examples are installed on the system disk: `hello.sz`, `files.sz`,
`draw.sz`, and `threads.sz`. At the guest shell:

    cc hello.sz hello
    hello

To write your own program:

    touch greeting.sz
    edit greeting.sz

Enter the following text, save with Ctrl+S, and leave the editor with Escape:

    int main() {
        print("Hello from SystemZero!\n");
        return 0;
    }

Then run:

    cc greeting.sz greeting
    greeting

Source and output must have different names. `cc` reads and writes on the
selected drive. The shell looks for executables on that drive first, then on
the system drive, so the bundled compiler remains available on another drive.
Output names do not require an extension.

A compile error reports `filename:line:column: message`, returns status 1,
and leaves an existing output untouched. A successful compile replaces the
output through the filesystem's staged replacement operation.

## Language

Supported types are signed 32-bit `int`, unsigned byte `char`, `void`, and
pointers to these types. Integer literals can be decimal or hexadecimal.
Character and string literals accept `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, `\'`,
and `\xHH`. Both `//` and `/* ... */` comments are supported.

Programs contain functions and optional global variables. Functions can call
later definitions or recurse; prototypes are optional. A function takes at
most eight scalar/pointer parameters. The entry function is either
`int main()` / `int main(void)` or `int main(char *args)`. `args` is the complete
command line, including the program name. Returning from `main` supplies the
shell's exit status; falling off a function returns zero.

Local and global scalar variables, comma-separated declarations, nested block
scopes, and one-dimensional arrays with literal lengths are supported:

    int sum = 0;
    char buffer[128];
    int numbers[4];
    int *p = numbers;
    p[0] = 10;
    *(p + 1) = 20;

Arrays decay to pointers when used in expressions. Address-of applies to
scalar variables and array elements (`&sum`, `&numbers[0]`). Pointer arithmetic
uses the size of the pointed-to type. Arithmetic/dereferencing on `void*` is
rejected; use a cast to a typed pointer first.

Global scalar initializers must be literal numbers/characters, optionally
signed. Global character arrays can use string initializers, including
`char name[] = "example";`. Local scalar variables without initializers start
at zero. Local arrays have no initializer syntax; assign their elements or
use `memset`.

Statements include blocks, expression statements, `if/else`, `while`, `break`,
`continue`, and `return`. Operators follow C precedence:

- Arithmetic: `+ - * / %`, unary `+ -`, prefix/postfix `++ --`.
- Comparison: `== != < <= > >=`.
- Bitwise: `& | ^ ~ << >>`; right shift of an integer is arithmetic.
- Boolean: `! && ||`, with short-circuit evaluation and results of zero or one.
- Assignment: `= += -= *= /= %=`.
- Pointers/indexing: `&x`, `*p`, `p[index]`.
- Explicit casts: `(char)value`, `(int)value`, `(char*)pointer`, and other
  supported scalar/pointer types.
- `sizeof` with a parenthesized type, such as `sizeof(int)` or `sizeof(char*)`.

Integer addition, subtraction, and multiplication keep the low 32 bits.
Division is signed and truncates toward zero. Characters keep the low eight
bits. Function arguments are evaluated left to right.

The first version does not support preprocessing/includes, floating point,
structures, general function-pointer declarations, multidimensional arrays, `for`, `switch`,
variadic functions, or separate object/library files. Built-in kernel services
are available by name without headers. User functions in the same source file
are linked by the compiler.
An `int worker(int argument)` function name can be passed to `thread_start`;
the compiler emits an entry wrapper that connects it to the kernel scheduler.

Current limits: 64 KiB source, 256 KiB executable, 64 KiB combined string/global
data, 16,384 tokens, 128 functions, 128 globals, 256 simultaneously visible
locals, and 16 KiB local storage per function. Nesting limits produce compile
errors instead of exhausting the compiler's stack.

## Kernel services

These names are reserved. `int` arguments/results and pointers are 32 bits.
File byte counts and offsets are bytes, unlike the older C kernel sector API.

| Call | Result |
| --- | --- |
| `print(char *text)` | Prints a NUL-terminated string; 0 on success. |
| `print_dec(int value)` | Prints a signed decimal integer; 0. |
| `print_hex(int value)` | Prints the unsigned bit pattern in hexadecimal; 0. |
| `putchar(int ch)` | Prints one byte; returns that byte. |
| `newline()` | Prints a newline; 0. |
| `clear()` | Clears the display; 0. |
| `set_color(int attribute)` | Sets the persistent terminal attribute (0..255); 0 or -1. |
| `get_color()` | Current terminal attribute. |
| `screen_rows()`, `screen_columns()` | Current text grid dimensions. |
| `put_text(int row, int col, char *text, int attribute)` | Writes at text coordinates; 0 or -1. |
| `put_cell(int row, int col, int ch, int attribute)` | Writes one text cell; 0 or -1. |
| `begin_update()`, `end_update()` | Batch cell changes into one presentation; 0. |
| `draw_pixel(int x, int y, int color)` | Writes a framebuffer pixel; 0 or -1. |
| `draw_line(int x1, int y1, int x2, int y2, int color)` | Draws a line; 0 or -1. |
| `alloc(int bytes)` | Returns `void*`, or 0 on failure. Memory lasts until the command returns. |
| `memcpy(void *dest, void *source, int bytes)` | Copies bytes; returns destination, or 0 for an invalid count/null buffer. |
| `memset(void *dest, int byte, int bytes)` | Fills bytes; returns destination, or 0 for an invalid count/null buffer. |
| `strlen(char *text)` | String length, or -1 for a null pointer. |
| `readline(char *buffer, int capacity)` | Reads and echoes a line; returns length or -1 on Escape/error. Capacity includes the terminator. |
| `getchar()` | Waits for a character without echo; Enter is 10 and Escape is 27. |
| `file_size(char *name)` | File size, or -1 if unavailable. |
| `file_read(char *name, void *buffer, int bytes, int offset)` | Bytes read (0 at EOF), or -1 on failure. |
| `file_write(char *name, void *buffer, int bytes)` | Creates/replaces a file, including binary zero bytes; 0 or -1. |
| `file_delete(char *name)` | Deletes a file; 0 or -1. |
| `file_create(char *name)` | Creates an empty file; 0 or -1 if it exists/cannot be created. |
| `sleep(int milliseconds)` | Waits 0..60,000 ms; 0 or -1. |
| `execute(char *command)` | Executes a command and returns its exit status. |
| `thread_start(void *entry, int argument)` | Starts a worker; returns a positive thread ID or -1. |
| `thread_join(int id)` | Waits for a worker and consumes its handle; returns its result or -1 for an invalid handle/self-join. |
| `thread_yield()` | Gives other runnable threads a turn; 0. |
| `thread_sleep(int milliseconds)` | Sleeps 0..60,000 ms; 0 or -1. Zero yields. |
| `thread_id()` | Current thread ID; the foreground thread is 0. |
| `thread_count()` | Number of live user workers. |
| `mutex_create()` | Creates a command-owned mutex; positive handle or -1. |
| `mutex_lock(int id)` | Waits for a mutex; 0 or -1 for an invalid/recursive lock. |
| `mutex_unlock(int id)` | Releases a mutex held by this thread; 0 or -1. |
| `clock_time()` | Seconds since midnight, 0..86,399. |
| `status_position()` | 0 for top, 1 for bottom. |
| `settings_apply(int seconds, int position, int attribute)` | Saves and applies clock/bar/color settings; 0 or -1. Requires different foreground/background colors. |

Text attributes use the upper nibble for background and lower nibble for
foreground. Pixel colors use the existing 0..15 palette. Graphics calls return
-1 if there is no framebuffer or the coordinates/color are invalid.
Callers provide appropriately sized buffers for memory and file operations.
The 12-pixel status bar is reserved: drawing into its active top/bottom margin
returns -1. Clearing the screen preserves the bar. `begin_update`/`end_update`
batch text cells; the clock/status bar continues updating independently.

## Threads

The kernel schedules threads on one CPU with a 5 ms timer quantum. Each worker
has a separate 64 KiB stack; programs share their code, globals, heap, filesystem,
and console. Up to 13 user workers can exist at once. This provides concurrency
on the existing shared address space; there is no process isolation, SMP
scheduling, or floating-point context support.

    int worker(int value) {
        thread_sleep(100);
        return value * 2;
    }
    int main() {
        int id = thread_start(worker, 21);
        if (id < 0) return 1;
        print_dec(thread_join(id));
        newline();
        return 0;
    }

The function must return `int` and take one `int` argument. A function name
evaluates to its thread entry wrapper; it can also be stored in a `void*`.
Arguments can carry 32-bit values or pointers cast to `int`. Use a mutex to
protect shared read/modify/write operations such as `total++`; `threads.sz`
demonstrates this. Mutexes are nonrecursive and belong to the current command.

The command waits for all its workers before unloading its executable and
reclaiming its heap, even if `main` returns without joining. Joined handles
become invalid and their slots can be reused. A returning worker releases any
mutexes it still holds. All command-owned mutexes are discarded when it exits.

Kernel API operations on files, memory, and text are serialized across workers.
Use a mutex around a multi-call console update to keep related output together.
`sleep` also uses the scheduler. Keyboard input, `execute`, and `settings_apply`
are reserved for the foreground thread; workers receive an error for these
calls. Host-built C workers use `int entry(env_vars_t *env, uint32_t argument)`
and call shared services through `env->api`.

## Status bar and settings

The status bar shows `SystemZero  v0.1.0`, the current command with arguments,
and a 24-hour `HH:MM:SS` clock. Nested commands restore their parent's title
when they finish; the idle title is `shell`. Long command titles are shortened
with an ellipsis while the clock stays visible.

Run `settings` to open the graphical settings screen. Tab/Shift+Tab selects
hours, minutes, seconds, bar position, foreground, or background. Arrow keys
adjust a field; digits enter clock values. The two color palettes show all
16 colors and a live text preview. Enter saves; Escape cancels and restores the
previous console. The framebuffer retains its 96×48 text grid; the VGA fallback
reserves one row and provides an 80×24 text viewport.

Settings are saved atomically in `systemzero.cfg` on the system drive and
survive builds and reboots. The clock initializes from the hardware RTC, plus
a saved time offset, so it continues advancing between boots. Invalid saved
settings are ignored. The version string is defined in `shared/version.h`.

## Native API and ELF interface

The public ABI is defined in `shared/sys_api.h` and `shared/sys_api.def`.
The kernel publishes a persistent API table through `env_vars_t.api`. Each
service has a permanent numeric slot, a versioned table header, and the uniform
i386 cdecl signature `(env, a, b, c, d, e) -> int32_t`. Pointer results use the
same 32-bit register. Unused arguments are zero.

The compiler checks function names, argument counts, and types, and emits calls
through that table. Generated entry code checks its magic, version, and minimum
entry count before calling `main`; an incompatible API returns exit status 2.
Compatible version-1 tables may append entries without changing existing slots.
The kernel owns the functions, so generated executables use the actual kernel
libraries rather than carrying copies of their implementation.

Executables use one ELF32 load segment. Code/global/string addresses are
relative to the image base; internal calls use relative displacements.
No fixed kernel addresses, Linux interpreter, or dynamic symbol resolver are
needed. The existing ELF loader validates and loads the result. Calls preserve
the i386 callee-saved registers and use 16-byte-aligned call frames.

The loader reclaims command allocations and restores terminal updates/cursor
state when a command returns, including after nested `execute` calls.
This first compiler links user functions within one source file and binds
kernel imports; arbitrary `.o`/`.a` linking is a separate future feature.
