/*********************************************
 * getopt(): translated from the BSD version *
 *********************************************/

var getopt = function(args, ostr) {
	var oli; // option letter list index
	if (typeof(getopt.place) == 'undefined')
		getopt.ind = 0, getopt.arg = null, getopt.place = -1;
	if (getopt.place == -1) { // update scanning pointer
		if (getopt.ind >= args.length || args[getopt.ind].charAt(getopt.place = 0) != '-') {
			getopt.place = -1;
			return null;
		}
		if (getopt.place + 1 < args[getopt.ind].length && args[getopt.ind].charAt(++getopt.place) == '-') { // found "--"
			++getopt.ind;
			getopt.place = -1;
			return null;
		}
	}
	var optopt = args[getopt.ind].charAt(getopt.place++); // character checked for validity
	if (optopt == ':' || (oli = ostr.indexOf(optopt)) < 0) {
		if (optopt == '-') return null; //  if the user didn't specify '-' as an option, assume it means null.
		if (getopt.place < 0) ++getopt.ind;
		return '?';
	}
	if (oli+1 >= ostr.length || ostr.charAt(++oli) != ':') { // don't need argument
		getopt.arg = null;
		if (getopt.place < 0 || getopt.place >= args[getopt.ind].length) ++getopt.ind, getopt.place = -1;
	} else { // need an argument
		if (getopt.place >= 0 && getopt.place < args[getopt.ind].length)
			getopt.arg = args[getopt.ind].substr(getopt.place);
		else if (args.length <= ++getopt.ind) { // no arg
			getopt.place = -1;
			if (ostr.length > 0 && ostr.charAt(0) == ':') return ':';
			return '?';
		} else getopt.arg = args[getopt.ind]; // white space
		getopt.place = -1;
		++getopt.ind;
	}
	return optopt;
}

/* // getopt() example
var c;
while ((c = getopt(arguments, "i:xy")) != null) {
	switch (c) {
		case 'i': print(getopt.arg); break;
		case 'x': print("x"); break;
		case 'y': print("y"); break;
	}
}
*/

/*********************
 * Special functions *
 *********************/

Math.lgamma = function(z) {
	var x = 0;
	x += 0.1659470187408462e-06 / (z+7);
	x += 0.9934937113930748e-05 / (z+6);
	x -= 0.1385710331296526     / (z+5);
	x += 12.50734324009056      / (z+4);
	x -= 176.6150291498386      / (z+3);
	x += 771.3234287757674      / (z+2);
	x -= 1259.139216722289      / (z+1);
	x += 676.5203681218835      / z;
	x += 0.9999999999995183;
	return Math.log(x) - 5.58106146679532777 - z + (z-0.5) * Math.log(z+6.5);
}

function _kf_gammap(s, z)
{
	var sum, x, k;
	for (k = 1, sum = x = 1.; k < 100; ++k) {
		sum += (x *= z / (s + k));
		if (x / sum < 1e-290) break;
	}
	return Math.exp(s * Math.log(z) - z - Math.lgamma(s + 1.) + Math.log(sum));
}

function _kf_gammaq(s, z)
{
	var C, D, f, KF_TINY = 1e-14;
	f = 1. + z - s; C = f; D = 0.;
	for (var j = 1; j < 100; ++j) {
		var a = j * (s - j), b = (j<<1) + 1 + z - s, d;
		D = b + a * D;
		if (D < KF_TINY) D = KF_TINY;
		C = b + a / C;
		if (C < KF_TINY) C = KF_TINY;
		D = 1. / D;
		d = C * D;
		f *= d;
		if (Math.abs(d - 1.) < 1e-290) break;
	}
	return Math.exp(s * Math.log(z) - z - Math.lgamma(s) - Math.log(f));
}

Math.gammap = function(s, z) { return z <= 1. || z < s? _kf_gammap(s, z) : 1. - _kf_gammaq(s, z); }
Math.gammaq = function(s, z) { return z <= 1. || z < s? 1. - _kf_gammap(s, z) : _kf_gammaq(s, z); }
Math.chi2 = function(d, x2) { return Math.gammaq(.5 * d, .5 * x2); }

Math.erfc = function(x)
{
	var expntl, z, p;
	z = Math.abs(x) * 1.41421356237309504880168872420969808;
	if (z > 37.) return x > 0.? 0. : 2.;
	expntl = Math.exp(z * z * - .5);
	if (z < 7.07106781186547524400844362104849039) // for small z
	    p = expntl * ((((((.03526249659989109 * z + .7003830644436881) * z + 6.37396220353165) * z + 33.912866078383) * z + 112.0792914978709) * z + 221.2135961699311) * z + 220.2068679123761)
			/ (((((((.08838834764831844 * z + 1.755667163182642) * z + 16.06417757920695) * z + 86.78073220294608) * z + 296.5642487796737) * z + 637.3336333788311) * z + 793.8265125199484) * z + 440.4137358247522);
	else p = expntl / 2.506628274631001 / (z + 1. / (z + 2. / (z + 3. / (z + 4. / (z + .65)))));
	return x > 0.? 2. * p : 2. * (1. - p);
}

Math.normald = function(x) { return .5 * Math.erfc(-x * 0.707106781186547524400844362104849039); }

/*********************
 * Matrix operations *
 *********************/

Math.m = {};

Math.m.T = function(a) { // matrix transpose
	var b = [], m = a.length, n = a[0].length; // m rows and n cols
	for (var j = 0; j < n; ++j) b[j] = [];
	for (var i = 0; i < m; ++i)
		for (var j = 0; j < n; ++j)
			b[j].push(a[i][j]);
	return b;
}

Math.m.print = function(a) { // print a matrix to stdout
	var m = a.length, n = a[0].length;
	for (var i = 0; i < m; ++i) {
		var line = '';
		for (var j = 0; j < n; ++j)
			line += (j? "\t" : '') + a[i][j];
		print(line);
	}
}

Math.m.mul = function(a, b) { // matrix mul
	var m = a.length, n = a[0].length, s = b.length, t = b[0].length;
	if (n != s) return null;
	var x = [], c = Math.m.T(b);
	for (var i = 0; i < m; ++i) {
		x[i] = [];
		for (var j = 0; j < t; ++j) {
			var sum = 0;
			var ai = a[i], cj = c[j];
			for (var k = 0; k < n; ++k) sum += ai[k] * cj[k];
			x[i].push(sum);
		}
	}
	return x;
}

Math.m.add = function(a, b) { // matrix add
	var m = a.length, n = a[0].length, s = b.length, t = b[0].length;
	var x = [];
	if (m != s || n != t) return null; // different dimensions
	for (var i = 0; i < m; ++i) {
		x[i] = [];
		var ai = a[i], bi = b[i];
		for (var j = 0; j < n; ++j)
			x[i].push(ai[j] + bi[j]);
	}
	return x;
}

Math.m.solve = function(a, b) { // Gauss-Jordan elimination, translated from gaussj() in Numerical Recipes in C.
	// on return, a[n][n] is the inverse; b[n][m] is the solution
	var n = a.length, m = (b)? b[0].length : 0;
	if (a[0].length != n || (b && b.length != n)) return -1; // invalid input
	var xc = [], xr = [], ipiv = [];
	var i, ic, ir, j, l, tmp;

	for (j = 0; j < n; ++j) ipiv[j] = 0;
	for (i = 0; i < n; ++i) {
		var big = 0;
		for (j = 0; j < n; ++j) {
			if (ipiv[j] != 1) {
				for (k = 0; k < n; ++k) {
					if (ipiv[k] == 0) {
						if (Math.abs(a[j][k]) >= big) {
							big = Math.abs(a[j][k]);
							ir = j; ic = k;
						}
					} else if (ipiv[k] > 1) return -2; // singular matrix
				}
			}
		}
		++ipiv[ic];
		if (ir != ic) {
			for (l = 0; l < n; ++l) tmp = a[ir][l], a[ir][l] = a[ic][l], a[ic][l] = tmp;
			if (b) for (l = 0; l < m; ++l) tmp = b[ir][l], b[ir][l] = b[ic][l], b[ic][l] = tmp;
		}
		xr[i] = ir; xc[i] = ic;
		if (a[ic][ic] == 0) return -3; // singular matrix
		var pivinv = 1. / a[ic][ic];
		a[ic][ic] = 1.;
		for (l = 0; l < n; ++l) a[ic][l] *= pivinv;
		if (b) for (l = 0; l < m; ++l) b[ic][l] *= pivinv;
		for (var ll = 0; ll < n; ++ll) {
			if (ll != ic) {
				var dum = a[ll][ic];
				a[ll][ic] = 0;
				for (l = 0; l < n; ++l) a[ll][l] -= a[ic][l] * dum;
				if (b) for (l = 0; l < m; ++l) b[ll][l] -= b[ic][l] * dum;
			}
		}
	}
	for (l = n - 1; l >= 0; --l)
		if (xr[l] != xc[l])
			for (var k = 0; k < n; ++k)
				tmp = a[k][xr[l]], a[k][xr[l]] = a[k][xc[l]], a[k][xc[l]] = tmp;
	return 0;
}

/* // Math.m example
x = [[1,2],[3,4]]; y = [[1,2],[3,4]];
Math.m.print(Math.m.add(Math.m.mul(x,y), x));
a = [[1,1],[1,-1]]; b = [[2],[0]];
a = [[1,2],[3,4]];
print(Math.m.solve(a));
Math.m.print(a);
*/

/************************************
 * Fasta/Fastq reader in Javascript *
 ************************************/

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
	this.c.size(0); this.s.size(0); this.q.size(0);
	if ((c = f.readline(this.n, 0)) < 0) return -1; // normal exit: EOF
	if (c != 10) f.readline(this.c); // read FASTA/Q comment
	if (this.s.capacity() == 0) this.s.capacity(256);
	while ((c = f.read()) != -1 && c != 62 && c != 43 && c != 64) {
		if (c == 10) continue; // skip empty lines
		this.s.set(c);
		f.readline(this.s, 2, true); // read the rest of the line
	}
	if (c == 62 || c == 64) this._last = c; // the first header char has been read
	if (c != 43) return this.s.size(); // FASTA
	this.q.capacity(this.s.capacity());
	c = f.readline(this._line); // skip the rest of '+' line
	if (c < 0) return -2; // error: no quality string
	var size = this.s.size();
	while (f.readline(this.q, 2, true) >= 0 && this.q.size() < size);
	f._last = 0; // we have not come to the next header line
	if (this.q.size() != size) return -2; // error: qual string is of a different length
	return size;
}

Fastx.prototype.destroy = function() {
	this.s.destroy(); this.q.destroy(); this.c.destroy(); this.n.destroy(); this._line.destroy();
	if (typeof(this._file.close) == 'object') this._file.close();
}

/**************************
 * Bioinformatics related *
 **************************/
/*
Bio = {};
Bio.Seq.comp_pair = ['WSATUGCYRKMBDHVNwsatugcyrkmbdhvn', 'WSTAACGRYMKVHDBNwstaacgrymkvhdbn'];
Bio.Seq.ts_table = {'AG':1, 'GA':1, 'CT':2, 'TC':2};
*/
/* // Bio.Seq example
var s = new Bio.Seq();
var f = new File(arguments[0]);
while (s.next(function(){return f.next()}) != null) s.print();
*/
