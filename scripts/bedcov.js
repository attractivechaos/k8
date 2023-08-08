#!/usr/bin/env k8

load("k8.js");

function main(args)
{
	if (args.length < 2) {
		print("Usage: bedcov.js <loaded.bed> <streamed.bed>");
		return;
	}
	let bed = {}, file, buf = new Bytes();
	file = new File(args[0]);
	while (file.readline(buf) >= 0) {
		const t = buf.toString().split("\t", 3);
		if (bed[t[0]] == null) bed[t[0]] = [];
		bed[t[0]].push([parseInt(t[1]), parseInt(t[2]), 0]);
	}
	for (const ctg in bed) iit_index(bed[ctg]);
	file.close();

	file = new File(args[1]);
	while (file.readline(buf) >= 0) {
		const t = buf.toString().split("\t", 3);
		if (bed[t[0]] == null) {
			print(t[0], t[1], t[2], 0, 0);
		} else {
			const st0 = parseInt(t[1]), en0 = parseInt(t[2]);
			const a = iit_overlap(bed[t[0]], st0, en0);
			let cov_st = 0, cov_en = 0, cov = 0;
			for (let i = 0; i < a.length; ++i) {
				const st1 = a[i][0] > st0? a[i][0] : st0;
				const en1 = a[i][1] < en0? a[i][1] : en0;
				if (st1 > cov_en) {
					cov += cov_en - cov_st;
					cov_st = st1, cov_en = en1;
				} else cov_en = cov_en > en1? cov_en : en1;
			}
			cov += cov_en - cov_st;
			print(t[0], t[1], t[2], a.length, cov);
		}
	}
	file.close();
	buf.destroy();
}

main(arguments);
