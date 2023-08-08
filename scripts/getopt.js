load("k8.js");

function main(args) {
	for (const o of getopt(args, "x:y", [ "foo=", "bar" ]))
		print(`${o.opt}=${o.arg}`);
}

main(arguments);
