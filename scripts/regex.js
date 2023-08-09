#!/usr/bin/env k8

function main(args) {
	if (args.length == 0) {
		print("Usage: k8 regex.js <pattern> [file.txt]");
		print("Patterns:");
		print(`  URI (protocol://server/path): ([a-zA-Z][a-zA-Z0-9]*)://([^\\s/]+)(/[^\\s]*)?`);
		print(`  URI|Email: ([a-zA-Z][a-zA-Z0-9]*)://([^\\s/]+)(/[^\\s]*)?|([^\\s@]+)@([^\\s@]+)`);
		print("Data:");
		print("  http://people.unipmn.it/manzini/lightweight/corpus/howto.bz2");
		exit(1);
	}
	let re = new RegExp(args[0]);
	let buf = new Bytes();
	let file = args.length >= 2? new File(args[1]) : new File();
	while (file.readline(buf) >= 0) {
		const line = buf.toString();
		if (line.match(re))
			print(line);
	}
	file.close();
	buf.destroy();
}

main(arguments);
