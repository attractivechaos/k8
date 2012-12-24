FAQ
---

####1. What is K8?

K8 is a Javascript shell based on Google's [V8 Javascript engine][1]. It adds
the support of flexible byte arrays, file I/O and convenient buffered input
stream. K8 is implemented in one C++ source file. The only dependency is V8 and
zlib.

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

K8 aims to provide C-like file I/O APIs. It adds a `File` object for low-level
file access and a `iStream` object that works in a similar way to Java's
[BufferedReader][10], wrapping any read-only stream. K8 also implements
flexible byte arrays. This is partly to resolve my concern about the lack of
mutable strings in Javascript.

####3. How to compile K8? Are there compiled binaries?

You need to first compile V8 and then compile and link K8. Here is the full procedure:

	git clone https://github.com/v8/v8             # download V8
	(cd v8; make dependencies; make x64.release)   # compile V8
	g++ -O2 -Wall -o k8 -Iv8/include k8.cc -lpthread -lz v8/out/*/libv8_{base,snapshot}.a

The two `libv8*.a` files should always be placed somewhere in `v8/out`, but
maybe in a deeper directory, depending on the OS.

Alternatively, you may download the compiled binaries for Mac and Linux from
[SourceForge][11]. The source code is also included.


API Documentations
------------------

All the following objects manage some memory outside the V8 garbage collector.
It is important to call the `close()` or the `destroy()` methods to deallocate
the memory to avoid memory leaks.

###The Bytes Object

`Bytes` provides a byte array class. It has the following methods:

    // Create a byte array of size $size, which defaults to 0
    new Bytes(size)
	// Get the size of the array if $size is undefined, or set the size of the array otherwise.
	Bytes.prototype.size(size)
	// The index operator. If $pos goes beyond size(), undefined will be returned.
	obj[pos]
	// Modify the array starting from $pos. $data can be a number, a string, an array or Bytes.
	// The byte array will be extended if there is not enough capacity.
    Bytes.prototype.set(data, pos)
	// Convert the byte array to string
	Bytes.prototype.toString()
	// Deallocate the array. This is necessary as the memory is not managed by the V8 GC.
	Bytes.prototype.destroy()

Here is an example:

    var ba = new Bytes();
	ba.set("foo"); ba.set([0x20, 0x20]); ba[4]=0x2c; ba.set("bar"); ba.set('F', 0);
	print(ba.size(), ba.toString())
	ba.destroy();

###The File Object

`File` provides basic unbuffered file I/O. It has the following methods:

	// Create a file handler for $fileName under $mode. The file can be optionally gzip/zlib
	// compressed. The mode is in the same syntax as in fopen() and defaults to "r".
	new File(fileName, mode)
	// Read $len characters and return as a string
	File.prototype.read(len)
	// Write $str and return the number of written bytes
	File.prototype.write(str)
	// Close the file
	File.prototype.close()

###The iStream Object

`iStream` is a generic stream buffer. It calls the `read` method of the
provided object to read and buffer a block of data and segments the input data
into lines or fields.

	// Create a stream from $obj. Method obj.read(len) must be present and return a string.
	new iStream(obj)
	// Read a line (if $sep is absent) or a field to byte-array $bytes with $sep as the delimitor.
	// In particular, $sep==0 to set the delimitor to isspace(), $sep==1 to set the delimitor to
	// (isspace() && !' ') and $sep==2 to newline. If $sep is a string, the first character in the
	// string is the delimitor. Return the line length or -1 if reaching end-of-file.
	iStream.prototype.readline(bytes, sep)
	// Close the stream. If obj.close() is a function, it will also be called.
	iStream.prototype.close()

Here is an example:

	var bytes = new Bytes()
	var stream = new iStream(new File('myfile.txt'))
	while (stream.readline(bytes) >= 0) print(bytes)
	stream.close()


[1]: http://code.google.com/p/v8/
[2]: http://nodejs.org/
[3]: https://github.com/tlrobinson/narwhal
[4]: http://silkjs.net/
[5]: http://code.google.com/p/teajs/
[6]: https://github.com/samlecuyer/sorrow.js
[7]: http://nodejs.org/api/fs.html
[8]: http://nodejs.org/api/stream.html
[9]: http://www.commonjs.org/specs/
[10]: http://docs.oracle.com/javase/6/docs/api/java/io/BufferedReader.html
[11]: https://sourceforge.net/projects/k8-shell/files/
