function sd_genmat() {
	let i, j, r, c2, C = [], R = [];
	for (i = r = 0; i < 9; ++i)
		for (j = 0; j < 9; ++j)
			for (k = 0; k < 9; ++k)
				C[r++] = [ 9 * i + j, (Math.floor(i/3)*3 + Math.floor(j/3)) * 9 + k + 81, 9 * i + k + 162, 9 * j + k + 243 ];
	for (c = 0; c < 324; ++c) R[c] = [];
	for (r = 0; r < 729; ++r)
		for (c2 = 0; c2 < 4; ++c2)
			R[C[r][c2]].push(r);
	return [R, C];
}

function sd_update(R, C, sr, sc, r, v) {
	let min = 10, min_c = 0;
	for (let c2 = 0; c2 < 4; ++c2) sc[C[r][c2]] += v<<7;
	for (let c2 = 0; c2 < 4; ++c2) {
		let rr, c = C[r][c2];
		if (v > 0) {
			for (let r2 = 0; r2 < 9; ++r2) {
				if (sr[rr = R[c][r2]]++ != 0) continue;
				for (let cc2 = 0; cc2 < 4; ++cc2) {
					let cc = C[rr][cc2];
					if (--sc[cc] < min)
						min = sc[cc], min_c = cc;
				}
			}
		} else { // revert
			for (let r2 = 0; r2 < 9; ++r2) {
				if (--sr[rr = R[c][r2]] != 0) continue;
				const p = C[rr];
				++sc[p[0]]; ++sc[p[1]]; ++sc[p[2]]; ++sc[p[3]];
			}
		}
	}
	return min<<16 | min_c; // return the col that has been modified and with the minimal available choices
}

function sd_solve(R, C, _s) {
	let hints = 0, sr = [], sc = [], cr = [], cc = [], out = [], ret = [];
	for (let r = 0; r < 729; ++r) sr[r] = 0;
	for (let c = 0; c < 324; ++c) sc[c] = 9;
	for (let i = 0; i < 81; ++i) {
		const a = _s[i] >= '1' && _s[i] <= '9'? _s.charCodeAt(i) - 49 : -1;
		if (a >= 0) sd_update(R, C, sr, sc, i * 9 + a, 1);
		if (a >= 0) ++hints;
		cr[i] = cc[i] = -1, out[i] = a + 1;
	}
	let i = 0, dir = 1, cand = 10<<16|0;
	for (;;) {
		while (i >= 0 && i < 81 - hints) {
			if (dir == 1) {
				let min = cand>>16;
				cc[i] = cand&0xffff;
				if (min > 1) {
					for (let c = 0; c < 324; ++c) {
						if (sc[c] < min) {
							min = sc[c], cc[i] = c;
							if (min <= 1) break;
						}
					}
				}
				if (min == 0 || min == 10) cr[i--] = dir = -1;
			}
			const c = cc[i];
			if (dir == -1 && cr[i] >= 0) sd_update(R, C, sr, sc, R[c][cr[i]], -1);
			let r2;
			for (r2 = cr[i] + 1; r2 < 9; ++r2)
				if (sr[R[c][r2]] == 0) break;
			if (r2 < 9) {
				cand = sd_update(R, C, sr, sc, R[c][r2], 1);
				cr[i++] = r2; dir = 1;
			} else cr[i--] = dir = -1;
		}
		if (i < 0) break;
		let y = [];
		for (let j = 0; j < 81; ++j) y[j] = out[j];
		for (let j = 0; j < i; ++j) r = R[cc[j]][cr[j]], y[Math.floor(r/9)] = r%9 + 1;
		ret.push(y);
		--i; dir = -1;
	}
	return ret;
}

function main(args) {
	let e = sd_genmat();
	if (args.length == 0) {
		warn("Usage: k8 sudoku.js <hard20.txt>");
		return;
	}
	let buf = new Bytes();
	let file = new File(args[0]);
	while (file.readline(buf) >= 0) {
		const l = buf.toString();
		if (l.length >= 81) {
			let r = sd_solve(e[0], e[1], l);
			for (let i = 0; i < r.length; ++i)
				print(r[i].join(''));
			print();
		}
	}
	file.close();
	buf.destroy();
}

main(arguments);
