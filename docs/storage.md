# Storage and environment

The shell starts in `sys:/home`. Use `mkdir`, `cd`, `pwd`, and `list` to work
with directories. `del` removes files and empty directories.

`tree` displays the current directory recursively. Use `tree sys:/home` or
`tree 1p1:/source` to select another directory or volume. Entries are sorted
by name, directories end with `/`, and the summary counts descendants.

```text
project
|-- assets/
|   `-- icon.dat
|-- main.sz
`-- notes.txt

1 directory, 3 files
```

## Disk layout

`os-image.bin` is one 166 MiB GPT disk used by both BIOS and UEFI.

| Volume | Format | Size    | Contents                        |
| ------ | ------ | ------: | ------------------------------- |
| `0p1:` | FAT32  | 64 MiB  | `EFI/BOOT/BOOTX64.EFI`          |
| `0p2:` | ESFS   | 100 MiB | System files and user workspace |

The EFI executable contains the kernel. ESFS commands access the system volume;
`diskman` displays both partitions. The system disk's partition table and both
boot/system volumes are protected while SystemZero runs.

## Filesystem layout

All paths below are on the system volume. Prefix a path with `sys:` to reach
it from another volume.

| Path                      | Contents                          |
| ------------------------- | --------------------------------- |
| `/bin`                    | Bundled commands                  |
| `/home`                   | User source files and programs    |
| `/home/examples`          | Editable source examples          |
| `/docs`                   | Manual, references, and license   |
| `/sys/config/environment` | Persistent variables and settings |
| `/sys/include/systemzero` | Guest compiler SDK headers        |
| `/sys/lib`                | SDK objects and `system.a`        |

## Paths and volumes

Paths are case sensitive. `/`, `.`, and `..` select the root, current directory,
and parent directory. A volume-qualified path is absolute. Relative paths use
that volume's working directory; each volume remembers it until reboot.

| Path                  | Location                         |
| --------------------- | -------------------------------- |
| `file.sz`             | Current directory                |
| `./app`               | Program in the current directory |
| `/bin/list`           | Selected volume's root           |
| `sys:/bin/list`       | System volume                    |
| `0p2:/home/file.sz`   | Partition 2 on disk 0            |
| `1p1:/source/file.sz` | Partition 1 on disk 1            |
| `1:/file.sz`          | Whole-disk ESFS volume on disk 1 |

`cd` without arguments selects `HOME`. `drive sys` selects the system volume;
`drive 1p1` selects partition 1 on disk 1. `pwd` prints the full path.

Example, with a formatted `1p1:` volume:

```text
mkdir 1p1:/source
copy sys:/home/examples/hello.sz 1p1:/source/hello.sz
cd 1p1:/source
cc hello.sz hello
./hello
cd
```

| Limit                | Value                        |
| -------------------- | ---------------------------- |
| Path length          | 511 bytes                    |
| File payload         | 916,992 bytes                |
| Mounted volume size  | Less than 4 GiB              |
| Physical disks       | 4                            |
| Partition slots/disk | 16                           |
| Supported layouts    | Whole disk, primary MBR, GPT |

ESFS supports binary file data and directory hierarchies. FAT files and extended
MBR partitions are unsupported. GPT CRCs are checked; a recovered backup table
or a table with unsupported attributes is read-only for partition edits. Replacing a file
needs space for both file versions. ESFS has no journal or guaranteed
power-loss recovery.

## Partitioning and formatting

Run `diskman` or `format`.

| Key        | Action                                   |
| ---------- | ---------------------------------------- |
| Left/Right | Select physical disk                     |
| Up/Down    | Select partition or whole-disk volume    |
| Digits     | Enter new partition size in MiB          |
| Backspace  | Remove a size digit                      |
| G / M      | Create an empty GPT / MBR table          |
| C          | Create a partition in free space         |
| D          | Delete the selected partition            |
| F          | Format the selected volume as ESFS       |
| Enter      | Apply the displayed pending operation    |
| Escape     | Cancel an operation or close the utility |

Creating a table replaces the disk layout. Deleting or formatting a volume
removes its files. A new partition needs formatting before file operations.

For three additional 100 MiB data disks:

```sh
./create_virtual_disks
./run_with_extra_disks
```

## Shell completion

Press Tab to extend the current word to its longest matching prefix. Repeated
Tab cycles through matches alphabetically; Shift+Tab cycles backwards. A file
completion ends with a space, and a directory completion ends with `/`.
Completion works at the caret and preserves other arguments. Names containing
spaces are quoted automatically. Commands accept quoted paths and backslash
escapes, for example `edit "two words.txt"`.

Command names complete from `PATH`. Argument rules live in
`sys:/sys/config/completion`, alongside `environment`. Edit it with
`zedit sys:/sys/config/completion`; changes apply on the next completion.

```text
list=paths
edit=paths
zedit=paths
hexedit=paths
cd=directories
mycommand=paths
```

| Value         | Argument completion             |
| ------------- | ------------------------------- |
| `paths`       | Files and directories           |
| `directories` | Directories only                |
| `none`        | Disabled                        |

Rules apply to every argument. Command keys use the executable's basename,
so `edit` also matches `/bin/edit`. Blank lines and `#` comments are allowed;
the last valid rule for a command wins. Commands without a rule do not complete
arguments. The default rules also cover `view`, `tree`, `copy`, `del`, `mkdir`,
`cc`, and `ld`.

The file can hold up to 8 KiB of rules. Completion extends the common prefix
across all matches and cycles the first 128 distinct matches in alphabetical
order. `make all` preserves custom rules; `make fresh-image` installs the
bundled defaults from `config/completion`.

## Environment variables

Settings are saved in `sys:/sys/config/environment`. Built-in settings are
validated before saving. Variable names use uppercase letters, digits, and
underscores; values are printable strings of up to 511 bytes. Up to 64 variables
can be stored. Custom variables use the same storage.

### Paths

Lists use semicolons. Search order follows the list order.

| Variable       | Default            | Purpose                   |
| -------------- | ------------------ | ------------------------- |
| `HOME`         | `sys:/home`        | Startup directory; `cd`   |
| `PATH`         | `sys:/bin`         | Bare command lookup       |
| `INCLUDE`      | `sys:/sys/include` | Compiler header lookup    |
| `LIBRARY_PATH` | `sys:/sys/lib`     | Object and archive lookup |

`HOME` must be an absolute path. Commands containing `/` or a volume qualifier
are used directly. Run local programs with `./app`.

```text
set PATH 1p1:/apps;sys:/bin
app
sys:/bin/settings
```

### Display and clock

| Variable           | Default | Allowed values / purpose        |
| ------------------ | ------- | ------------------------------- |
| `COLOR_FOREGROUND` | `15`    | Text palette index, 0-15        |
| `COLOR_BACKGROUND` | `1`     | Background palette index, 0-15  |
| `COLOR_SECONDARY`  | `14`    | Prompt/logo palette index, 0-15 |
| `STATUS_POSITION`  | `top`   | `top` or `bottom`               |
| `CLOCK_OFFSET`     | `0`     | RTC offset in seconds, 0-86,399 |

Both text colors must differ from the background. The clock displays
24-hour time and initializes from the hardware RTC plus `CLOCK_OFFSET`.

### Read-only values

| Variable        | Value                                 |
| --------------- | ------------------------------------- |
| `PWD`           | Current volume-qualified directory    |
| `SYSTEM_VOLUME` | System volume address, usually `0p2:` |
| `VERSION`       | Kernel version                        |

### Editing settings

| Command                       | Action                               |
| ----------------------------- | ------------------------------------ |
| `set NAME`                    | Display a value                      |
| `set NAME VALUE`              | Save a value; spaces are supported   |
| `env`                         | Open the environment editor          |
| `settings`                    | Open clock, position, color controls |
| `color BACKGROUND FOREGROUND` | Save terminal palette indices        |

In `env`, Up/Down selects a variable, Tab selects name/value, Ctrl+N creates a
variable, Ctrl+S saves, and Escape exits. In `settings`, Tab/Shift+Tab selects a
field, arrows adjust it, digits enter time, Enter saves, and Escape cancels.
Both utilities restore the saved console on exit.

## Host image tools

Stop QEMU before updating its disk. `ESFS_raw_write` automatically selects the
system ESFS volume. Prefix an operation with `--volume NUMBER` for another
partition. Paths inside the tool are relative to the selected volume's root.

```sh
./ESFS_raw_write --replace os-image.bin home/app.sz app.sz
./ESFS_raw_write --append os-image.bin home/notes.txt more.txt
./ESFS_raw_write --extract os-image.bin home/app.sz exported.sz
./ESFS_raw_write --mkdir os-image.bin home/project
./ESFS_raw_write --delete os-image.bin home/notes.txt
./ESFS_raw_write --list os-image.bin home
./ESFS_raw_write --check os-image.bin
./ESFS_raw_write --geometry os-image.bin
```

`--geometry` prints JSON. `--delete` succeeds if the path is absent. An operation
without a flag appends to the named file.

Create a whole-disk ESFS data volume:

```sh
truncate -s 100M data.bin
./ESFS_raw_write --format data.bin
```

Formatting removes filesystem contents and retains the volume's reserved boot
sectors. The tool rejects locked images and corrupt filesystem metadata.
Normal builds preserve user files/settings; `make fresh-image` resets them.

For headers, library objects, and linking examples, see
[the programming reference](language.md#libraries-and-linking).
