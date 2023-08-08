if (arguments.length == 0) {
	warn("Usage: k8 lc.js <in.txt>");
	exit(1);
}
let fp = k8_open(arguments[0]);
if (fp == null)
	throw Error(`Failed to open file "${arguments[0]}"`);
let line, n_lines = 0;
while ((line = k8_readline(fp)) != null)
	++n_lines;
k8_close(fp);
print(n_lines);
