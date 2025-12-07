/*********************************
 * Command-line argument parsing *
 *********************************/

Array.prototype.delete_at = function(i) {
	for (let j = i; j < this.length - 1; ++j)
		this[j] = this[j + 1];
	--this.length;
}

function* getopt(argv, ostr, longopts) {
	if (argv.length == 0) return;
	let pos = 0, cur = 0;
	while (cur < argv.length) {
		let lopt = "", opt = "?", arg = "";
		while (cur < argv.length) { // skip non-option arguments
			if (argv[cur][0] == "-" && argv[cur].length > 1) {
				if (argv[cur] == "--") cur = argv.length;
				break;
			} else ++cur;
		}
		if (cur == argv.length) break;
		let a = argv[cur];
		if (a[0] == "-" && a[1] == "-") { // a long option
			pos = -1;
			let c = 0, k = -1, tmp = "", o;
			const pos_eq = a.indexOf("=");
			if (pos_eq > 0) {
				o = a.substring(2, pos_eq);
				arg = a.substring(pos_eq + 1);
			} else o = a.substring(2);
			for (let i = 0; i < longopts.length; ++i) {
				let y = longopts[i];
				if (y[y.length - 1] == "=") y = y.substring(0, y.length - 1);
				if (o.length <= y.length && o == y.substring(0, o.length)) {
					k = i, tmp = y;
					++c; // c is the number of matches
					if (o == y) { // exact match
						c = 1;
						break;
					}
				}
			}
			if (c == 1) { // find a unique match
				lopt = tmp;
				if (pos_eq < 0 && longopts[k][longopts[k].length-1] == "=" && cur + 1 < argv.length) {
					arg = argv[cur+1];
					argv.delete_at(cur + 1);
				}
			}
		} else { // a short option
			if (pos == 0) pos = 1;
			opt = a[pos++];
			let k = ostr.indexOf(opt);
			if (k < 0) {
				opt = "?";
			} else if (k + 1 < ostr.length && ostr[k+1] == ":") { // requiring an argument
				if (pos >= a.length) {
					arg = argv[cur+1];
					argv.delete_at(cur + 1);
				} else arg = a.substring(pos);
				pos = -1;
			}
		}
		if (pos < 0 || pos >= argv[cur].length) {
			argv.delete_at(cur);
			pos = 0;
		}
		if (lopt != "") yield { opt: `--${lopt}`, arg: arg };
		else if (opt != "?") yield { opt: `-${opt}`, arg: arg };
		else yield { opt: "?", arg: "" };
	}
}

/******************
 * Interval query *
 ******************/

function iit_index(a) {
	if (a.length == 0) return -1;
	a.sort(function(x, y) { return x[0] - y[0] });
	let last, last_i, k;
	for (let i = 0; i < a.length; i += 2) last = a[i][2] = a[i][1], last_i = i;
	for (k = 1; 1<<k <= a.length; ++k) {
		const i0 = (1<<k) - 1, step = 1<<(k+1);
		for (let i = i0; i < a.length; i += step) {
			const x = 1<<(k-1);
			a[i][2] = a[i][1];
			if (a[i][2] < a[i-x][2]) a[i][2] = a[i-x][2];
			let e = i + x < a.length? a[i+x][2] : last;
			if (a[i][2] < e) a[i][2] = e;
		}
		last_i = last_i>>k&1? last_i - (1<<(k-1)) : last_i + (1<<(k-1));
		if (last_i < a.length) last = last > a[last_i][2]? last : a[last_i][2];
	}
	return k - 1;
}

function iit_overlap(a, st, en) {
	let h = 0, stack = [], b = [];
	for (h = 0; 1<<h <= a.length; ++h);
	--h;
	stack.push([(1<<h) - 1, h, 0]);
	while (stack.length) {
		const t = stack.pop();
		const x = t[0], h = t[1], w = t[2];
		if (h <= 3) {
			const i0 = x >> h << h;
			let i1 = i0 + (1<<(h+1)) - 1;
			if (i1 >= a.length) i1 = a.length;
			for (let i = i0; i < i1 && a[i][0] < en; ++i)
				if (st < a[i][1]) b.push(a[i]);
		} else if (w == 0) { // if left child not processed
			stack.push([x, h, 1]);
			const y = x - (1<<(h-1));
			if (y >= a.length || a[y][2] > st)
				stack.push([y, h - 1, 0]);
		} else if (x < a.length && a[x][0] < en) {
			if (st < a[x][1]) b.push(a[x]);
			stack.push([x + (1<<(h-1)), h - 1, 0]);
		}
	}
	return b;
}

/****************
 * FASTX reader *
 ****************/

Fastx = function(f) {
	this._file = f;
	this._last = 0;
	this._line = new Bytes();
	this._finished = false;
	this.s = new Bytes();
	this.q = new Bytes();
	this.n = new Bytes();
	this.c = new Bytes();
}

Fastx.prototype.read = function() {
	var c, f = this._file, line = this._line;
	if (this._last == 0) { // then jump to the next header line
		while ((c = f.read()) != -1 && c != 62 && c != 64);
		if (c == -1) return -1; // end of file
		this._last = c;
	} // else: the first header char has been read in the previous call
	this.c.length = this.s.length = this.q.length = 0;
	if ((c = f.readline(this.n, 0)) < 0) return -1; // normal exit: EOF
	if (c != 10) f.readline(this.c); // read FASTA/Q comment
	if (this.s.capacity == 0) this.s.capacity = 256;
	while ((c = f.read()) != -1 && c != 62 && c != 43 && c != 64) {
		if (c == 10) continue; // skip empty lines
		this.s.set(c);
		f.readline(this.s, 2, this.s.length); // read the rest of the line
	}
	if (c == 62 || c == 64) this._last = c; // the first header char has been read
	if (c != 43) return this.s.length; // FASTA
	this.q.capacity = this.s.capacity;
	c = f.readline(this._line); // skip the rest of '+' line
	if (c < 0) return -2; // error: no quality string
	var size = this.s.length;
	while (f.readline(this.q, 2, this.q.length) >= 0 && this.q.length < size);
	f._last = 0; // we have not come to the next header line
	if (this.q.length != size) return -2; // error: qual string is of a different length
	return size;
}

/************
 * AVL tree *
 ************/

class AVLnode {
	constructor(data) {
		this.data = data;
		this.p = [null, null];
		this.balance = 0;
	}
}

class AVLiter {
	constructor(tree) {
		this.tree = tree;
		this.stack = [];
	}
	#next_bidir(dir) {
		if (this.stack.length == 0) return null;
		dir = dir == 0? 0 : 1;
		let p = this.stack[this.stack.length-1].p[dir];
		if (p != null) { // go down
			for (; p != null; p = p.p[1 - dir])
				this.stack.push(p);
			return true;
		} else { // go up
			do {
				p = this.stack.pop();
			} while (this.stack.length > 0 && p == this.stack[this.stack.length - 1].p[dir]);
			return (this.stack.length > 0);
		}
	}
	get() { return this.stack.length? this.stack[this.stack.length - 1].data : null; }
	next() { return this.#next_bidir(1); }
	prev() { return this.#next_bidir(0); }
}

class AVLtree {
	constructor(cmp) {
		this.root = null;
		this.cmp = cmp;
		this.size = 0;
	}
	find(d) {
		let p = this.root;
		while (p != null) {
			const cmp = this.cmp(d, p.data);
			if (cmp < 0) p = p.p[0];
			else if (cmp > 0) p = p.p[1];
			else return p;
		}
		return null;
	}
	find_itr(d) {
		let itr = new AVLiter(this);
		let p = this.root;
		const has_d = typeof d == "undefined" || d == null? false : true;
		while (p != null) {
			itr.stack.push(p);
			const cmp = has_d? this.cmp(d, p.data) : -1;
			if (cmp < 0) p = p.p[0];
			else if (cmp > 0) p = p.p[1];
			else break;
		}
		return itr;
	}
	interval(d) {
		let p = this.root, l = null, u = null;
		while (p != null) {
			const cmp = this.cmp(d, p.data);
			if (cmp < 0) u = p, p = p.p[0];
			else if (cmp > 0) l = p, p = p.p[1];
			else { l = u = p; break; }
		}
		return [l, p, u];
	}
	#rotate1(p, dir) { // one rotation: (a,(b,c)q)p => ((a,b)p,c)q
		const opp = 1 - dir;
		let q = p.p[opp];
		p.p[opp] = q.p[dir];
		q.p[dir] = p;
		return q;
	}
	#rotate2(p, dir) { // two rotations: (a,((b,c)r,d)q)p => ((a,b)p,(c,d)q)r
		const opp = 1 - dir;
		let q = p.p[opp], r = q.p[dir];
		p.p[opp] = r.p[dir];
		r.p[dir] = p;
		q.p[dir] = r.p[opp];
		r.p[opp] = q;
		const b1 = dir == 0? +1 : -1;
		if (r.balance == b1) q.balance = 0, p.balance = -b1;
		else if (r.balance == 0) q.balance = p.balance = 0;
		else q.balance = b1, p.balance = 0;
		r.balance = 0;
		return r;
	}
	insert(d) {
		let stack = [], path = [];
		let bp = this.root, bq = null;
		let which = 0, q = bq;
		for (let p = bp; p; q = p, p = p.p[which]) { // find the insertion location
			const cmp = this.cmp(d, p.data);
			if (cmp == 0) return p; // the data is in the tree; insert nothing
			if (p.balance != 0)
				bq = q, bp = p, stack.length = 0;
			which = cmp > 0? 1 : 0;
			stack.push(which);
			path.push(p);
		}
		++this.size;
		let x = new AVLnode(d); // create a new node to insert
		if (q == null) this.root = x;
		else q.p[which] = x;
		if (bp == null) return x;
		for (let p = bp, i = 0; p != x; p = p.p[stack[i]], ++i) { // update balance factors
			if (stack[i] == 0) --p.balance;
			else ++p.balance;
		}
		if (bp.balance > -2 && bp.balance < 2) return x; // no re-balance needed
		// re-balance
		let r;
		which = bp.balance < 0? 1 : 0;
		const b1 = which == 0? +1 : -1;
		q = bp.p[1 - which];
		if (q.balance == b1) {
			r = this.#rotate1(bp, which);
			q.balance = bp.balance = 0;
		} else r = this.#rotate2(bp, which);
		if (bq == null) this.root = r;
		else bq.p[bp != bq.p[0]? 1 : 0] = r;
		return x;
	}
	erase(d) {
		let path = [], dir = [];
		let fake = new AVLnode(null);
		fake.p[0] = this.root;
		let p = fake;
		if (d != null) {
			for (let cmp = -1; cmp != 0; cmp = this.cmp(d, p.data)) {
				const which = cmp > 0? 1 : 0;
				dir.push(which);
				path.push(p);
				p = p.p[which];
				if (p == null) return null; // nothing to delete: not present in the tree
			}
		} else {
			for (; p != null; p = p.p[0]) {
				dir.push(0);
				path.push(p);
			}
			p = path.pop();
		}
		--this.size;
		if (p.p[1] == null) { // ((1,.)2,3)4 => (1,3)4; p=2
			path[path.length-1].p[dir[dir.length-1]] = p.p[0];
		} else {
			let q = p.p[1];
			if (q.p[0] == null) { // ((1,2)3,4)5 => ((1)2,4)5; p=3
				q.p[0] = p.p[0];
				q.balance = p.balance;
				path[path.length-1].p[dir[dir.length-1]] = q;
				path.push(q);
				dir.push(1);
			} else { // ((1,((.,2)3,4)5)6,7)8 => ((1,(2,4)5)3,7)8; p=6
				let r, e = path.length;
				for (;;) {
					dir.push(0);
					path.push(q);
					r = q.p[0];
					if (r.p[0] == null) break;
					q = r;
				}
				r.p[0] = p.p[0];
				q.p[0] = r.p[1];
				r.p[1] = p.p[1];
				r.balance = p.balance;
				path[e-1].p[dir[e-1]] = r;
				path.splice(e, 0, r);
				dir.splice(e, 0, 1);
			}
		}
		while (path.length > 0) {
			let q = path.pop();
			let which = dir.pop(), other = 1 - which;
			let b1 = 1, b2 = 2;
			if (which) b1 = -b1, b2 = -b2;
			q.balance += b1;
			if (q.balance == b1) break;
			else if (q.balance == b2) {
				let r = q.p[other];
				if (r.balance == -b1) {
					path[path.length-1].p[dir[dir.length-1]] = this.#rotate2(q, which);
				} else {
					path[path.length-1].p[dir[dir.length-1]] = this.#rotate1(q, which);
					if (r.balance == 0) {
						r.balance = -b1;
						q.balance = b1;
						break;
					} else r.balance = q.balance = 0;
				}
			}
		}
		this.root = fake.p[0];
		return p;
	}
}

/********************
 * Simpler File I/O *
 ********************/

function* k8_readline(fn) {
	let buf = new Bytes();
	let file = new File(fn);
	while (file.readline(buf) >= 0) {
		yield buf.toString();
	}
	file.close();
	buf.destroy();
}
