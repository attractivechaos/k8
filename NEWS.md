Release 1.2-r135 (27 May 2024)
------------------------------

New functions:

 * `read_file()`: read an entire file as an ArrayBuffer
 * `decode()`: convert an ArrayBuffer/Bytes object to a string
 * `encode()`: convert a string to an ArrayBuffer

Improved:

 * `Bytes.read()`: more flexible API to read the rest of a file

(1.2: 27 May 2024, r135)



Release 1.1-r129 (26 May 2024)
------------------------------

This version sets v8's default `max_old_space_size` to 16 GB and also adds
option `-m` as a more convenient way to change this parameter.

(1.1: 26 May 2024, r129)



Release 1.0-r124 (10 August 2023)
---------------------------------

The previous version of k8, v0.2.5, was built on top of v8-3.16.4 released on
2013-01-11. This version updates v8 to v8-10.2.154.26 released on 2023-01-23,
ten years later. It brings ES6 features including but not limited to the "let"
keyword to define local variables, Java-like classes and back-tick strings.

Due to the lack of the SetIndexedPropertiesToExternalArrayData() API in v8, it
is not possible to keep full backward compatibility with older k8 versions.
Nonetheless, we have tried to retain commonly used methods such that several
popular k8 scripts can mostly work.

New functions:

 * `k8_version()`: get the k8 version
 * `Bytes.buffer`: get ArrayBuffer

Improved:

 * `load()`: search the script path
 * `print()`: print to stdout. Support ArrayBuffer. Faster for Bytes.
 * `warn()`: print to stderr. Support ArrayBuffer. Faster for Bytes.

Mostly unchanged:

 * `exit()`: exit
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

 * `Map` - JavaScript has `Map` and works better
 * `Bytes.prototype.cast()` - Bytes only supports `uint8_t`
 * `Bytes[]` - not possible to implement. Use `Bytes.buffer` as a partial remedy

(1.0: 10 August 2023, r124)
