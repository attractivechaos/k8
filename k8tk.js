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

Math.kernel_smooth = function(a, radius, func)
{
	if (a.length == 0) return null;
	if (func == null) func = function(x) { return x > -1 && x < 1? .75 * (1 - x * x) : 0; }
	a.sort(function(x,y) { return x[0]-y[0] });
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
	var c, col_g = null, col_x = 0, cols = [], radius = 1, precision = 6, missing = "NA";
	while ((c = getopt(args, "r:g:x:y:p:")) != null) {
		if (c == 'r') radius = parseFloat(getopt.arg);
		else if (c == 'g') col_g = parseInt(getopt.arg) - 1;
		else if (c == 'x') col_x = parseInt(getopt.arg) - 1;
		else if (c == 'p') precision = parseInt(getopt.arg);
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
		print("Usage: k8 k8tk.js ksmooth [-r radius] [-g groupCol] [-x xCol] [-y yCol] <in.txt>");
		return 1;
	}
	if (cols.length == 0) cols = [1];
	cols.unshift(col_x);

	var buf = new Bytes();
	var file = args[getopt.ind] == '-'? new File() : new File(args[getopt.ind]);
	var group = {}, list = [];
	while (file.readline(buf) >= 0) {
		var t = buf.toString().split("\t");
		var key = col_g == null? "*" : t[col_g];
		if (group[key] == null) group[key] = [];
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

	var smooth = {};
	for (var key in group)
		smooth[key] = Math.kernel_smooth(group[key], radius);
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

function main(args)
{
	if (args.length == 0) {
		print("Usage: k8 k8tk.js <command> <arguments>");
		print("Commands:");
		print("  Numerical:");
		print("    spearman       Spearman correlation");
		print("    ksmooth        kernel smoothing");
		print("  Bioinformatics:");
		print("    markmut        Mark mutation type (e.g. ts/tv/cpg/etc)");
		return 1;
	}
	var cmd = args.shift();
	if (cmd == "spearman") return k8_spearman(args);
	else if (cmd == "ksmooth") return k8_ksmooth(args);
	else if (cmd == "markmut") return k8_markmut(args);
	else {
		throw Error("ERROR: unknown command '" + cmd + "'");
	}
	return 0;
}

exit(main(arguments));
