Release 1.0-r106 (7 August 2023)
--------------------------------

The previous version of k8, v0.2.5, was built on top of v8-3.16.4 released on
2013-01-11. This version updates v8 to v8-10.2.154.26 released on 2023-01-23,
ten years later. It brings ES6 features including but limited to the "let"
keyword to define local variables, Java-like classes and back-tick strings.

Due to the lack of the SetIndexedPropertiesToExternalArrayData() API in v8, it
is not possible to keep full backward compatibility with older k8 versions.
Nonetheless, we have tried to retain commonly used methods such that the
several popular k8 scripts can mostly work. We also introduced new file I/O
functions to avoid clashes with the File classes in other JavaScript runtimes.

New functions:

 * `k8_open()`: open a file
 * `k8_close()`: close an opened file
 * `k8_getc()`: read a byte
 * `k8_read()`: read bytes
 * `k8_readline()`: read a line
 * `k8_readfastx()`: read a FASTX record
 * `k8_write()`: write to a plain file
 * `k8_version()`: get the k8 version
 * `Bytes.buffer`: get ArrayBuffer

Improved:

 * `load()`: search the script path

Unchanged:

 * `print()`: print to stdout
 * `warn()`: print to stderr
 * `exit()`: exit the entire program

Unchanged but not recommended:

 * `Bytes.length`: get/set Bytes length
 * `Bytes.capacity`: get/set Bytes capacity
 * `new File()`: open a file
 * `File.prototype.close()`: close the file handler
 * `File.prototype.read()`: read a byte or bytes
 * `File.prototype.readline()`: read a line
 * `File.prototype.write()`: write to a plain file

Changed functions (BREAKING):

 * `new Bytes()` - Bytes only supports `uint8_t` now
 * `Bytes.prototype.set()` - Bytes only supports `uint8_t`

Removed functions (BREAKING):

 * `Map` - it is a JavaScript object now
 * `Bytes.prototype.cast()` - Bytes only supports `uint8_t`
 * `Bytes[]` - not possible to implement. Use `Bytes.buffer` as a partial remedy

(0.3: 24 June 2023, r237)
