#!/usr/bin/env k8

"use strict";
load("k8.js");

const str = "MNOLKQOPHIA";
let avl = new AVLtree((x, y) => x == y? 0 : x < y? -1 : 1);
for (let i = 0; i < str.length; ++i)
	avl.insert(str[i]);
print(avl.size);
let itr = avl.find_itr("K");
while (itr.get() != null) {
	print(itr.get());
	itr.next();
}
avl.erase("O");
print(avl.size);
