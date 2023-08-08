Release 0.3-r97 (7 August 2023)
-------------------------------

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

 * `k8_open()` for file opening
 * `k8_close()` for closing an opened file
 * `k8_getc()` for reading a byte
 * `k8_read()` for reading bytes
 * `k8_readline()` for reading a line
 * `k8_readfastx()` for reading a FASTX record
 * `k8_write()` for writing to a plain file
 * `k8_version()` for getting the k8 version
 * `Bytes.buffer` for getting ArrayBuffer

Unchanged:

 * `print()` for printing to stdout
 * `warn()` for printing to stderr
 * `exit()` for exiting the entire program
 * `load()` for loading an external source file

Unchanged but not recommended:

 * `Bytes.length` for getting/setting Bytes length
 * `Bytes.capacity` for getting/setting Bytes capacity
 * `new File()` for file opening
 * `File.prototype.close()` for closing an opened file
 * `File.prototype.read()` for reading a byte or bytes
 * `File.prototype.readline()` for reading a line
 * `File.prototype.write()` for closing an opened file

Changed functions (BREAKING):

 * `new Bytes()`: Bytes only supports `uint8_t` now
 * `Bytes.prototype.set()`: Bytes only supports `uint8_t`

Removed functions (BREAKING):

 * The `Map` object as it is a JavaScript object now
 * `Bytes.prototype.cast()`: Bytes only supports `uint8_t`
 * `Bytes[]`: not possible to implement. Use `Bytes.buffer` as a partial remedy

(0.3: 24 June 2023, r237)
