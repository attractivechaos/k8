#!/usr/bin/env k8

load("k8.js");

function main(args) {
	if (args.length == 0) {
		print("Usage: fqcnt.js <in.fq.gz>");
		return 1;
	}
	var file = new File(args[0]);
	var fx = new Fastx(file);
	var n = 0, slen = 0, qlen = 0;
	while (fx.read() >= 0)
		++n, slen += fx.s.length, qlen += fx.q.length;
	file.close();
	print(n, slen, qlen);
}

main(arguments);
