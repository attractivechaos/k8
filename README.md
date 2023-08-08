## Getting Started
```sh
# Download precomiplied binaries
wget -O- link-to-be-added-later | tar -jxf -
k8-0.3.0/k8-Linux -e 'print(Math.log(2))'

# Compile from source code
wget -O- https://nodejs.org/dist/v18.17.0/node-v18.17.0.tar.gz | tar -zxf -
cd node-v18.17.0
git clone https://github.com/attractivechaos/k8
cd k8 && make
```

The following example counts the number of lines:
```javascript
if (arguments.length == 0) {
	warn("Usage: k8 this-prog.js <in.txt>");
	exit(1);
}
let fp = k8_open(arguments[0]);
if (fp == null) throw Error(`Failed to open file "${arguments[0]}"`);
let line, n_lines = 0;
while ((line = k8_readline(fp)) != null)
	++n_lines;
k8_close(fp);
print(n_lines);
```

## Introduction

K8 is a Javascript runtime built on top of Google's [V8 Javascript engine][v8].
It provides synchronous APIs for plain file writing and gzip'd file reading. It
also parses the FASTA/FASTQ format used in Bioinformatics.

## Motivations

Javascript is among the fastest scripting languages. It is essential for web
development but not often used for large-scale text processing or command-line
utilities, in my opinion, due to the lack of sensible file I/O.  Current
Javascript runtimes such as [Node.js][node] and [Deno][deno] focus on
[asynchronous I/O][aio] and whole-file reading. Even reading a file line by
line, which is required to work with large files, becomes a cumbersome effort.
K8 aims to solve this problem. With synchronous I/O APIs, Javascript is in fact
a powerful language for developing command-line tools.

## API Documentations

### Functions

Since v0.3.0, we recommend the following functions to read/write files.
Class-based APIs are still provided for backward compatibility.

```typescript
// open a plain or gzip'd file for reading or a plain file for writing.
// $encoding affects the return value of k8_read() and k8_readline()
// $encoding=1 (default) for Latin-1 string, 2 for UTF8 string and 0 for ArrayBuffer
function k8_open(fileName?: string, mode?: string, encoding?: number) :object

// close an opened file and free internal buffers
function k8_close(fp: object)

// read a byte
function k8_getc(fp: object) :number

// read bytes. Return type determined by $encoding on k8_open()
function k8_read(fp: object, len: number) :string|ArrayBuffer

// read a line. $delimiter=0 for spaces, 1 for TAB and 2 for line
function k8_readline(fp: object, delimiter?: number|string) :string|ArrayBuffeer

// read a FASTA/FASTQ record. Return a [name,seq,qual,comment] array
function k8_readfastx(fp: object) :Array

// write $data and return the number of bytes written
function k8_write(fp: object, data: string|ArrayBuffer) :number

// print to stdout (print) or stderr (warn). TAB delimited
function print(str1, str2)
function warn(str1, str2)

// exit
function exit(code: number)

// load a Javascript file and execute
function load(fileName: string)
```

### The Bytes Object

`Bytes` provides a byte array. **Not recommended** as Javascript has
[ArrayBuffer][arraybuffer] and [TypedArray][typedarray] now.

```typescript
// Create an array of byte buffer of $len in size. 
new Bytes(len?: number)

// Property: get/set length of the array
.length: number

// Property: get/set the max capacity of the array
.capacity: number

// Deallocate the array. This is necessary as the memory is not managed by the V8 GC.
Bytes.prototype.destroy()

// Replace the byte array starting from $offset to $data, where $data can be a number,
// a string, an array or Bytes. The size of the array is modified if the new array
// is larger. Return the number of modified bytes.
Bytes.prototype.set(data: number|string|Array, offset?: number) :number

// Convert the byte array to string
Bytes.prototype.toString()
```

### The File Object

`File` provides buffered file I/O. It clashes with File in other runtimes and
has been mostly replaced by `k8_open()` functions since v0.3.0. **Not
recommended**.

```javascript
new File(fileName?: string, mode?: string)
File.prototype.read() :number
File.prototype.read(buf: Bytes, offset :number, len :number) :number
File.prototype.readline(bytes, sep?, offset?) :number
File.prototype.write(data: string|ArrayBuffer) :number
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
