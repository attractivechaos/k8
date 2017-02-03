/***************************
 *** k8 library routines ***
 ***************************/

/////////// General ///////////

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

/////////// Interval manipulation ///////////

Interval = {};

Interval.sort = function(a)
{
	if (typeof a[0] == 'number')
		a.sort(function(x, y) { return x - y });
	else a.sort(function(x, y) { return x[0] - y[0] });
}

Interval.merge = function(a, to_srt)
{
	if (typeof to_srt == 'undefined') to_srt = true;
	if (to_srt) Interval.sort(a);
	var k = 0;
	for (var i = 1; i < a.length; ++i) {
		if (a[k][1] >= a[i][0])
			a[k][1] = a[k][1] > a[i][1]? a[k][1] : a[i][1];
		else a[++k] = a[i].slice(0);
	}
	a.length = k + 1;
}

Interval.find_intv = function(a, x)
{
	var left = -1, right = a.length;
	if (typeof a[0] == 'number') {
		while (right - left > 1) {
			var mid = left + ((right - left) >> 1);
			if (a[mid] > x) right = mid;
			else if (a[mid] < x) left = mid;
			else return mid;
		}
	} else {
		while (right - left > 1) {
			var mid = left + ((right - left) >> 1);
			if (a[mid][0] > x) right = mid;
			else if (a[mid][0] < x) left = mid;
			else return mid;
		}
	}
	return left;
}

Interval.trans_coor = function(a, x)
{
	var k = Interval.find_intv(a, x);
	if (k < 0) return a[0][1];
	if (k == a.length - 1) return a[a.length - 1][1];
	return a[k][1] + (a[k+1][1] - a[k][1]) * ((x - a[k][0]) / (a[k+1][0] - a[k][0]));
}

Interval.overlap = function(a, st, en)
{
	if (en <= a[0][0]) return 0;
	if (st >= a[a.length-1][1]) return 0;
	var k_st = Interval.find_intv(a, st);
	var k_en = Interval.find_intv(a, en);
	if (k_st == k_en) {
		return st >= a[k_st][1]? 0 : (a[k_en][1] < en? a[k_en][1] : en) - st;
	} else {
		var len = k_st < 0 || st >= a[k_st][1]? 0 : a[k_st][1] - st;
		for (var k = k_st + 1; k < k_en; ++k)
			len += a[k][1] - a[k][0];
		len += (a[k_en][1] < en? a[k_en][1] : en) - a[k_en][0];
		return len;
	}
}

/////////// Numerical ///////////

Math.spearman = function(a)
{
	function aux_func(t) { return t == 1? 0 : (t * t - 1) * t / 12; }
	var x = [], T = [], S = [];
	for (var i = 0; i < a.length; ++i)
		x[i] = [a[i][0], a[i][1], 0, 0];
	for (var k = 0; k < 2; ++k) {
		x.sort(function(a,b){return a[k]-b[k]});
		var same = 1;
		T[k] = 0;
		for (var i = 1; i <= x.length; ++i) {
			if (i < x.length && x[i-1][k] == x[i][k]) ++same;
			else {
				var rank = (i<<1) - same + 1;
				for (var j = i - same; j < i; ++j) x[j][k+2] = rank;
				if (same > 1) T[k] += aux_func(same), same = 1;
			}
		}
		S[k] = aux_func(x.length) - T[k];
	}
	var d2 = 0.;
	for (var i = 0; i < x.length; ++i)
		d2 += .25 * (x[i][2] - x[i][3]) * (x[i][2] - x[i][3]);
	return .5 * (S[0] + S[1] - d2) / Math.sqrt(S[0] * S[1]);
}

Math.kernel_smooth = function(a, radius, bound, func)
{
	if (a.length == 0) return null;
	if (typeof func == "undefined" || func == null)
		func = function(x) { return x > -1 && x < 1? .75 * (1 - x * x) : 0; } // Epanechnikov
	a.sort(function(x,y) { return x[0]-y[0] });
	if (typeof bound != "undefined")
		bound[0] = a[0][0], bound[1] = a[a.length-1][0];
	var r1 = 1.0 / radius;
	return function(x) {
		// binary search
		var L = 0, R = a.length - 1;
		while (R - L > 1) {
			var m = Math.floor((L + R) / 2);
			if (a[m][0] < x) L = m + 1;
			else if (a[m][0] > x) R = m - 1;
			else {
				L = R = m;
				break;
			}
		}
		// smooth
		var b = [];
		for (var j = 0; j < a[0].length; ++j) b[j] = 0;
		for (var i = L; i < a.length && a[i][0] - x <= radius; ++i) {
			var w = func((x - a[i][0]) * r1);
			b[0] += w;
			for (var j = 1; j < a[i].length; ++j)
				b[j] += w * a[i][j];
		}
		for (var i = L - 1; i >= 0 && x - a[i][0] <= radius; --i) {
			var w = func((x - a[i][0]) * r1);
			b[0] += w;
			for (var j = 1; j < a[i].length; ++j)
				b[j] += w * a[i][j];
		}
		return b;
	}
}

/*************************
 *** k8tk applications ***
 *************************/

function k8_spearman(args)
{
	var c, col1 = 1, col2 = 2, missing = "NA";
	while ((c = getopt(args, "1:2:m:")) != null) {
		if (c == '1') col1 = parseInt(getopt.arg);
		else if (c == '2') col2 = parseInt(getopt.arg);
		else if (c == 'm') missing = getopt.arg;
	}
	if (args.length == getopt.ind) {
		print("Usage: k8 k8tk.js spearman [-1 col1] [-2 col2] <in.txt>");
		return 1;
	}
	--col1, --col2;

	var buf = new Bytes();
	var file = args[getopt.ind] == '-'? new File() : new File(args[getopt.ind]);
	var a = [];
	while (file.readline(buf) >= 0) {
		var t = buf.toString().split("\t");
		if (t.length <= col1 || t.length <= col2) continue;
		if (t[col1] == missing || t[col2] == missing) continue;
		a.push([parseFloat(t[col1]), parseFloat(t[col2])]);
	}
	file.close();
	buf.destroy();
	print(Math.spearman(a));
	return 0;
}

function k8_ksmooth(args)
{
	var c, col_g = null, col_x = 0, cols = [], radius = 1, precision = 6, missing = "NA", step = null, use_uniform = false;
	while ((c = getopt(args, "r:g:x:y:p:s:u")) != null) {
		if (c == 'r') radius = parseFloat(getopt.arg);
		else if (c == 'g') col_g = parseInt(getopt.arg) - 1;
		else if (c == 'x') col_x = parseInt(getopt.arg) - 1;
		else if (c == 'p') precision = parseInt(getopt.arg);
		else if (c == 'u') use_uniform = true;
		else if (c == 's') step = parseInt(getopt.arg);
		else if (c == 'y') {
			var m, s = getopt.arg.split(",");
			for (var i = 0; i < s.length; ++i) {
				if ((m = /^(\d+)$/.exec(s[i])) != null)
					cols.push(parseInt(m[1]) - 1);
				else if ((m = /^(\d+)-(\d+)$/.exec(s[i])) != null) {
					var st = parseInt(m[1]) - 1, en = parseInt(m[2]);
					for (var j = st; j < en; ++j)
						cols.push(j);
				}
			}
		}
	}
	if (args.length == getopt.ind) {
		print("Usage: k8 k8tk.js ksmooth [options] <in.txt>");
		print("Options:");
		print("  -r FLOAT    radius [1]");
		print("  -g INT      group column []");
		print("  -x INT      x column [1]");
		print("  -y STR      y column(s) [2]");
		print("  -u          use uniform kernel [Epanechnikov]");
		print("  -s INT      step []");
		print("  -p INT      precision [6]");
		return 1;
	}
	if (cols.length == 0) cols = [1];
	cols.unshift(col_x);

	var buf = new Bytes();
	var file = args[getopt.ind] == '-'? new File() : new File(args[getopt.ind]);
	var group = {}, list = [], list_key = [];
	while (file.readline(buf) >= 0) {
		var t = buf.toString().split("\t");
		var key = col_g == null? "*" : t[col_g];
		if (group[key] == null) {
			group[key] = [];
			list_key.push(key);
		}
		var b = [];
		for (var i = 0; i < cols.length; ++i) {
			if (t[cols[i]] == missing) break;
			b.push(parseFloat(t[cols[i]]));
		}
		list.push([key, b[0]]);
		if (b.length < cols.length) continue;
		group[key].push(b);
	}
	file.close();
	buf.destroy();

	function kernel_uniform(x) { return x >= -1 && x < 1? 0.5 : 0 }

	var smooth = {}, bound = {};
	for (var key in group) {
		bound[key] = [];
		smooth[key] = Math.kernel_smooth(group[key], radius, bound[key], use_uniform? kernel_uniform : null);
	}
	if (step != null) {
		for (var k = 0; k < list_key.length; ++k) {
			var key = list_key[k], st = bound[key][0], en = bound[key][1];
			for (var x = st + (step>>1); x < en - (step>>1); x += step) {
				var b = smooth[key](x);
				var out = [];
				if (col_g != null) out.push(key);
				out.push(x);
				for (var j = 1; j < b.length; ++j)
					out.push(b[0] > 0? (b[j] / b[0]).toFixed(precision) : "NA");
				print(out.join("\t"));
			}
		}
	} else {
		for (var i = 0; i < list.length; ++i) {
			var b = smooth[list[i][0]](list[i][1]);
			var out = [];
			if (col_g != null) out.push(list[i][0]);
			out.push(list[i][1]);
			for (var j = 1; j < b.length; ++j)
				out.push(b[0] > 0? (b[j] / b[0]).toFixed(precision) : "NA");
			print(out.join("\t"));
		}
	}
	return 0;
}

function k8_binavg(args)
{
	var c, col_srt = 0, col_thres = -1, thres = 10, precision = 6;
	while ((c = getopt(args, "s:t:m:p:")) != null) {
		if (c == 's') col_srt = parseInt(getopt.arg) - 1;
		else if (c == 't') col_thres = parseInt(getopt.arg) - 1;
		else if (c == 'm') thres = parseFloat(getopt.arg);
		else if (c == 'p') precision = parseInt(getopt.arg);
	}
	if (args.length == getopt.ind) {
		print("Usage: k8 k8tk.js binavg [-s colSrt] [-t colThres] [-m minThres] <in.txt>");
		return 1;
	}

	var buf = new Bytes();
	var file = args[getopt.ind] == '-'? new File() : new File(args[getopt.ind]);
	var n_col = null, a = [];
	while (file.readline(buf) >= 0) {
		var t = buf.toString().split("\t");
		if (n_col == null) n_col = t.length;
		else if (n_col != t.length) throw Error("ERROR: lines with a different number of fields: " + n_col + " != " + t.length);
		for (var i = 0; i < t.length; ++i)
			t[i] = parseFloat(t[i]);
		a.push(t);
	}
	file.close();
	buf.destroy();

	a.sort(function(x, y) { return x[col_srt] - y[col_srt] });
	var sum = [], n = 0;
	for (var j = 0; j < n_col; ++j) sum[j] = 0;
	for (var i = 0; i < a.length; ++i) {
		for (var j = 0; j < n_col; ++j)
			sum[j] += a[i][j];
		++n;
		if ((col_thres < 0 && n >= Math.floor(thres + .499)) || (col_thres >= 0 && sum[col_thres] >= thres)) {
			for (var j = 0; j < n_col; ++j)
				sum[j] = (sum[j] / n).toFixed(precision);
			print(sum.join("\t"));
			for (var j = 0; j < n_col; ++j) sum[j] = 0;
			n = 0;
		}
	}
	if ((col_thres < 0 && n >= Math.floor(thres/2 + .499)) || (col_thres >= 0 && sum[col_thres] >= thres/2)) {
		for (var j = 0; j < n_col; ++j)
			sum[j] = (sum[j] / n).toFixed(precision);
		print(sum.join("\t"));
	}
	return 0;
}

/////////// Bioinformatics ///////////

function k8_markmut(args)
{
	if (args.length < 2) {
		print("Usage: k8 k8tk.js markmut <ref.fa> <mut.tsv>");
		return 1;
	}

	var A = "A".charCodeAt(0), C = "C".charCodeAt(0);
	var G = "G".charCodeAt(0), T = "T".charCodeAt(0);

	var file, buf = new Bytes();

	warn("Reading the reference genome...");
	var seqs = {}, seq = null;
	file = new File(args[0]);
	while (file.readline(buf) >= 0) {
		if (buf[0] == 62) {
			var m, line = buf.toString();
			if ((m = /^>(\S+)/.exec(line)) == null)
				throw Error("ERROR: wrong FASTA format");
			seq = seqs[m[1]] = new Bytes();
		} else seq.set(buf);
	}
	file.close();

	warn("Processing the list...");
	file = new File(args[1]);
	while (file.readline(buf) >= 0) {
		var t = buf.toString().split("\t");
		var type = [];
		var x = parseInt(t[1]) - 1;
		var seq = seqs[t[0]];
		if (seq != null && x >= 0 && x < seq.length) {
			var ref = t[2], alt = t[3].split(",");
			for (var j = 0; j < alt.length; ++j) {
				if (ref.length > alt[j].length) type.push("del");
				else if (ref.length < alt[j].length) type.push("ins");
				else if (ref.length > 1) type.push("mnp");
				else {
					var mut = ref < alt[j]? ref + alt[j] : alt[j] + ref;
					if (mut == "AG" || mut == "CT") {
						var is_cpg = false;
						if (mut == "AG" && x > 0 && seq[x-1] == C) is_cpg = true;
						else if (mut == "CT" && x < seq.length - 1 && seq[x+1] == G) is_cpg = true;
						type.push(is_cpg? "ts_cpg" : "ts_non_cpg");
					} else type.push("tv");
				}
			}
		}
		print(t.join("\t"), type.length? type.join(",") : "NA");
	}
	buf.destroy();
	file.close();
}

function k8_bedmerge(args)
{
	var buf = new Bytes();
	var file = args.length > 0? new File(args[0]) : new File();
	var ch = null, st, en;
	while (file.readline(buf) >= 0) {
		var t = buf.toString().split("\t", 3);
		var s = parseInt(t[1]);
		var e = parseInt(t[2]);
		if (ch != t[0] || s > en) { // no overlap
			if (ch != null) print(ch, st, en);
			ch = t[0], st = s, en = e;
		} else if (s < st) throw Error("ERROR: input is not sorted by coordinate");
		else en = en > e? en : e;
	}
	if (ch != null) print(ch, st, en);
	file.close();
	buf.destroy();
	return 0;
}

function k8_bedovlp(args)
{
	var c, min_len = 1, print_olen = false, skip_char = '#', print_hdr = false;
	while ((c = getopt(args, "pHS:l:")) != null) {
		if (c == 'l') min_len = /^(\d+)$/.test(getopt.arg)? parseInt(getopt.arg) : null;
		else if (c == 'p') print_olen = true;
		else if (c == 'S') skip_char = getopt.arg;
		else if (c == 'H') print_hdr = true;
	}
	if (args.length - getopt.ind < 2) {
		print("Usage: k8 k8tk.js bedovlp [options] <loaded.bed> <streamed.bed>");
		print("Options:");
		print("  -l INT     min overlap length, or 'c' for contained [1]");
		print("  -S STR     characters marking header lines [#]");
		print("  -p         print the overlap length as the last field on each line");
		print("  -H         print header lines");
		return 1;
	}

	var file, buf = new Bytes();

	var bed = {};
	file = new File(args[getopt.ind]);
	while (file.readline(buf) >= 0) {
		var t = buf.toString().split("\t", 3);
		if (t.length < 2) continue;
		if (bed[t[0]] == null) bed[t[0]] = [];
		var st = parseInt(t[1]), en = null;
		if (t.length >= 3 && /^(\d+)$/.test(t[2]))
			en = parseInt(t[2]);
		if (en == null) en = st--;
		bed[t[0]].push([st, en]);
	}
	file.close();
	for (var key in bed)
		Interval.merge(bed[key]);

	file = new File(args[getopt.ind+1]);
	while (file.readline(buf) >= 0) {
		var line = buf.toString();
		var len = 0, t = line.split("\t", 3);
		var is_hdr = skip_char.indexOf(t[0].charAt(0)) < 0? false : true;
		if (!is_hdr && bed[t[0]] != null) {
			var st = parseInt(t[1]), en = parseInt(t[2]);
			len = Interval.overlap(bed[t[0]], st, en);
		}
		if (print_olen) {
			if (is_hdr) print(line);
			else print(line, len);
		} else {
			if (is_hdr && print_hdr) print(line);
			else if (!is_hdr) {
				var l = min_len != null && min_len < en - st? min_len : en - st;
				if (len >= l) print(line);
			}
		}
	}
	file.close();

	buf.destroy();
	return 0;
}

function main(args)
{
	if (args.length == 0) {
		print("Usage: k8 k8tk.js <command> <arguments>");
		print("Commands:");
		print("  Numerical:");
		print("    spearman       Spearman correlation");
		print("    ksmooth        Kernel smoothing");
		print("    binavg         Binned average");
		print("  Bioinformatics:");
		print("    markmut        Mark mutation type (e.g. ts/tv/cpg/etc)");
		print("    bedmerge       Merge overlaps in sorted BED");
		print("    bedovlp        Overlap length between two BED files");
		return 1;
	}
	var cmd = args.shift();
	if (cmd == "spearman") return k8_spearman(args);
	else if (cmd == "ksmooth") return k8_ksmooth(args);
	else if (cmd == "binavg") return k8_binavg(args);
	else if (cmd == "markmut") return k8_markmut(args);
	else if (cmd == "bedmerge") return k8_bedmerge(args);
	else if (cmd == "bedovlp") return k8_bedovlp(args);
	else {
		throw Error("ERROR: unknown command '" + cmd + "'");
	}
	return 0;
}

exit(main(arguments));
