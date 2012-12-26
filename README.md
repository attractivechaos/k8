FAQ
---

####1. What is K8?

K8 is a Javascript shell based on Google's [V8 Javascript engine][1]. It adds
the support of flexible byte arrays and file I/O. K8 is implemented in one C++
source file. The only dependency is zlib in addition to V8.

####2. There are many Javascript shells with much richer features. What makes K8 special?

To some extent, [Node.js][2], [Narwhal][3], [SilkJS][4], [TeaJS][5] and
[Sorrow.js][6] are all Javascript shells. They not only provide binary storage
and file I/O, the features available in K8, but also implement much richer
functionality such as network I/O and database binding. However, most of the
existing Javascript shells are designed for server-side applications, but not
for general use cases as we do with Perl/Ruby/Python.  Take the popular Node.js
as an example. Node.js mixes file I/O and file system operations, two distinct
concepts, in one [File System module][7].  In the module, we can either read an
entire file or a fixed-length data blob, but are unable to read a line as is
provided by most other programming languages. Many other JS shell
implementations follow the [CommonJS APIs][9], which have a similar problem: no
usable APIs for general-purpose file I/O. After all these efforts, on file I/O,
we even do not have a JS shell matching the usability of C, let alone
high-level programming languages such as Perl and Python.

K8 aims to provide C-like file I/O APIs. It adds a `File` object for buffered
file reading and a `Bytes` object for flexible binary storage.

####3. How to compile K8? Are there compiled binaries?

You need to first compile V8 and then compile and link K8. Here is the full procedure:

	git clone https://github.com/v8/v8             # download V8
	(cd v8; make dependencies; make x64.release)   # compile V8
	g++ -O2 -Wall -o k8 -Iv8/include k8.cc -lpthread -lz v8/out/*/libv8_{base,snapshot}.a

The two `libv8*.a` files should always be placed somewhere in `v8/out`, but
maybe in a deeper directory, depending on the OS.

Alternatively, you may download the compiled binaries for Mac and Linux from
[SourceForge][11]. The source code is also included.

####4. An earlier version of K8 implemented a generic buffered stream. Why has it been removed?

To implement a generic buffered stream, we need to call a Javascript `read`
function in C++ and transform between Javascript and C++ data representation.
This procedure adds significant overhead. For the best performance on file
I/O, all the `iStream` functionality has been moved to `File`. Anyway, it
is not hard to implement buffered stream purely in Javascript.


API Documentations
------------------

All the following objects manage some memory outside the V8 garbage collector.
It is important to call the `close()` or the `destroy()` methods to deallocate
the memory to avoid memory leaks.

###Example

    var x = new Bytes(), y = new Bytes();
    x.set('foo'); x.set([0x20,0x20]); x.set('bar'); x.set('F', 0); x[3]=0x2c;
    print(x.toString())   // output: 'Foo, bar'
    y.set('BAR'); x.set(y, 5)
    print(x)              // output: 'Foo, BAR'
    x.destroy(); y.destroy()
    if (arguments.length) { // read and print file
      var x = new Bytes(), s = new File(arguments[0]);
      while (s.readline(x) >= 0) print(x)
      s.close(); x.destroy();
    }

###The Bytes Object

`Bytes` provides a byte array. It has the following methods:

    // Create a zero-sized byte array
    new Bytes()

	// Create a byte array of length $len
	new Bytes(len)

	// Get the size of the array
	int Bytes.prototype.size()

	// Set the size of the array to $len
	int Bytes.prototype.size(len)

	// The index operator. If $pos goes beyond size(), undefined will be returned.
	int obj[pos]

	// Replace the byte array starting from $offset to $data, where $data can be a number,
	// a string, an array or Bytes. The size of the array is modified if the new array
	// is larger. Return the number of modified bytes. If only one byte needs to be
	// changed, using the [] operator gives better performance.
    int Bytes.prototype.set(data, offset)

	// Append $data to the byte array
	int Bytes.prototype.set(data)

	// Convert the byte array to string
	Bytes.prototype.toString()

	// Deallocate the array. This is necessary as the memory is not managed by the V8 GC.
	Bytes.prototype.destroy()

###The File Object

`File` provides buffered file I/O. It has the following methods:

	// Open STDIN. The input stream can be optionally gzip/zlib compressed.
	new File()

	// Open $fileName for reading
	new File(fileName)

	// Open $fileName under $mode. $mode is in the same syntax as fopen().
	new File(fileName, mode)

	// Read a byte. Return -1 if reaching end-of-file
	int File.prototype.read()

	// Read maximum $len bytes of data to $buf, starting from $offset. Return the number of
	// bytes read to $buf. The size of $buf is unchanged unless it is smaller than $offset+$len.
	int File.prototype.read(buf, offset, len)

	// Write $data, which can be a string or Bytes(). Return the number of written bytes.
	// This method replies on C's fwrite() for buffering.
	int File.prototype.write(data)

	// Read a line to $bytes. Return the line length or -1 if reaching end-of-file.
	int File.prototype.readline(bytes)

	// Read a line to $bytes using $sep as the separator. In particular, $sep==0 sets the
	// separator to isspace(), $sep==1 to (isspace() && !' ') and $sep==2 to newline. If
	// $sep is a string, the first character in the string is the separator.
	int File.prototype.readline(bytes, sep)

	// Close the file
	File.prototype.close()

[1]: http://code.google.com/p/v8/
[2]: http://nodejs.org/
[3]: https://github.com/tlrobinson/narwhal
[4]: http://silkjs.net/
[5]: http://code.google.com/p/teajs/
[6]: https://github.com/samlecuyer/sorrow.js
[7]: http://nodejs.org/api/fs.html
[8]: http://nodejs.org/api/stream.html
[9]: http://www.commonjs.org/specs/
[11]: https://sourceforge.net/projects/k8-shell/files/
