## Getting Started
```sh
# Download precomiplied binaries
wget -O- https://github.com/attractivechaos/k8/releases/download/v1.1/k8-1.1.tar.bz2 | tar -jxf -
k8-1.1/k8-x86_64-Linux -e 'print(Math.log(2))'

# Compile from source code. This requires to compile node.js first:
wget -O- https://nodejs.org/dist/v18.19.1/node-v18.19.1.tar.gz | tar -zxf -
cd node-v18.19.1 && ./configure && make -j16
# Then compile k8
git clone https://github.com/attractivechaos/k8
cd k8 && make
```

The following example counts the number of lines:
```javascript
if (arguments.length == 0) { // test command-line arguments
	warn("Usage: k8 lc.js <in.txt>");
	exit(1);
}
let buf = new Bytes();
let n = 0, file = new File(arguments[0]);
while (file.readline(buf) >= 0) ++n;
file.close();
buf.destroy();
print(n);
```

## Introduction

K8 is a JavaScript runtime built on top of Google's [v8 JavaScript engine][v8].
It provides a resizable binary buffer and synchronous APIs for plain file
writing and gzip'd file reading.

## Motivations

JavaScript is among the fastest scripting languages. It is essential for web
development but not often used for large-scale text processing or command-line
utilities, in my opinion, due to the lack of sensible file I/O.  Current
JavaScript runtimes such as [Node.js][node] and [Deno][deno] focus on
[asynchronous I/O][aio] and whole-file reading. Even reading a file line by
line, which is required to work with large files, becomes a cumbersome effort.
K8 aims to solve this problem. With synchronous I/O APIs, JavaScript can be a
powerful language for developing command-line tools.

## Installation

It is recommended to download precompiled binaries. If you want to compile k8,
you need to compile Node.js which bundles v8 and provides a more convenient way
build v8. As the v8 APIs are fast changing, both Node.js and k8 only work with
specific versions of v8. The k8-1.x branch is known to work with node-18.x but
not 19.x or higher. It is also worth noting that node-18.20.x upgraded
[c-ares][c-ares] which is now incompatible with older glibc. Node-18.19.1 is
the most recent version that can be compiled on CentOS 7.

## API Documentations

### Functions

```javascript
// Print to stdout (print) or stderr (warn). TAB delimited if multiple arguments.
function print(data: any)
function warn(data: any)

// Exit
function exit(code: number)

// Load a JavaScript file and execute. It searches the working directory, the
// script directory and then the K8_PATH environment variable in order.
function load(fileName: string)
```

### The Bytes Object

`Bytes` provides a resizable byte array.

```javascript
// Create an array of byte buffer of $len in size. 
new Bytes(len?: number = 0)

// Property: get/set length of the array
.length: number

// Property: get/set the max capacity of the array
.capacity: number

// Property: get ArrayBuffer of the underlying data, not allocated from v8
.buffer: ArrayBuffer

// Deallocate the array. This is necessary as the memory is not managed by the v8 GC.
Bytes.prototype.destroy()

// Replace the byte array starting from $offset to $data, where $data can be a number,
// a string, an array or Bytes. The size of the array is modified if the new array
// is larger. Return the number of modified bytes.
Bytes.prototype.set(data: number|string|Array|ArrayBuffer, offset?: number) :number

// Convert the byte array to string
Bytes.prototype.toString()
```

### The File Object

`File` provides buffered file I/O.

```javascript
// Open a plain or gzip'd file for reading or a plain file for writing. $file
// is file descriptor if it is an integer or file name if string. Each File
// object can only be read or only be written, not mixed
new File(file?: string|number = 0, mode?: string = "r")

// Read a byte and return it
File.prototype.read() :number

// Read $len bytes into $buf at $offset. Return the number of bytes read.
File.prototype.read(buf: Bytes, offset: number, len: number) :number

// Read a line or a token to $buf at $offset. $sep=0 for SPACE, 1 for TAB and 2
// for newline. If $sep is a string, only the first character is considered.
// Return the delimiter if non-negative, -1 upon EOF, or <-1 for errors
File.prototype.readline(buf: Bytes, sep?: number|string = 2, offset?: number = 0) :number

// Write data
File.prototype.write(data: string|ArrayBuffer) :number

// Close a file
File.prototype.close()
```

[3]: https://github.com/tlrobinson/narwhal
[4]: http://silkjs.net/
[5]: http://code.google.com/p/teajs/
[6]: https://github.com/samlecuyer/sorrow.js
[7]: http://nodejs.org/api/fs.html
[8]: http://nodejs.org/api/stream.html
[11]: https://sourceforge.net/projects/lh3/files/
[v8]: https://v8.dev
[gyp]: https://gyp.gsrc.io/
[release]: https://github.com/attractivechaos/k8/releases
[deno]: https://deno.land
[node]: https://nodejs.org/
[commjs]: https://en.wikipedia.org/wiki/CommonJS
[aio]: https://en.wikipedia.org/wiki/Asynchronous_I/O
[typedarray]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/TypedArray
[arraybuffer]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/ArrayBuffer
[c-ares]: https://c-ares.org
