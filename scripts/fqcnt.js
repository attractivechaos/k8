function main(args) {
	let r, fp = k8_open(args[0]);
	let n = 0, slen = 0, qlen = 0;
	while ((r = k8_readfastx(fp)) != null)
		++n, slen += r[1].length, qlen += r[2].length;
	k8_close(fp);
	print(n, slen, qlen);
}

main(arguments);
