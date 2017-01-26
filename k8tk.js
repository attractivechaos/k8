/***************************
 *** k8 library routines ***
 ***************************/

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

function main(args)
{
	if (args.length == 0) {
		print("Usage: k8 k8tk.js <command> <arguments>");
		print("Commands:");
		print("  spearman       Spearman correlation");
		return 1;
	}
	var cmd = args.shift();
	if (cmd == "spearman") return k8_spearman(args);
	else {
		throw Error("ERROR: unknown command '" + cmd + "'");
	}
	return 0;
}

exit(main(arguments));
