if (arguments.length == 0) {
	warn("Usage: k8 lc.js <in.txt>");
	exit(1);
}
let buf = new Bytes();
let n = 0, file = new File(arguments[0]);
while (file.readline(buf) >= 0) ++n;
file.close();
buf.destroy();
print(n);
