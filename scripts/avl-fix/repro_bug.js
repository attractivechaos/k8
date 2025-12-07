#!/usr/bin/env k8
"use strict";
load("../k8.js");

// Helper to print tree and check balances
function check_avl(node) {
    if (!node) return { h: 0, ok: true };

    let l = check_avl(node.p[0]);
    let r = check_avl(node.p[1]);

    if (!l.ok || !r.ok) return { h: 0, ok: false };

    let h = Math.max(l.h, r.h) + 1;
    let bal = r.h - l.h;

    if (bal != node.balance) {
        print("ERROR: Node " + node.data + " has stored balance " + node.balance + " but actual balance " + bal);
        return { h: h, ok: false };
    }

    if (bal < -1 || bal > 1) {
        print("ERROR: Node " + node.data + " is unbalanced (" + bal + ")");
        return { h: h, ok: false };
    }

    return { h: h, ok: true };
}

let avl = new AVLtree((x, y) => x - y);

// Insert nodes to form the specific structure
//        40
//      /    \
//    20      60
//   /  \    /  \
//  10  30  45  70
//         /
//        42

let keys = [40, 20, 60, 10, 30, 45, 70, 42];
for (let k of keys) avl.insert(k);

print("Tree size before delete: " + avl.size);
let res = check_avl(avl.root);
if (res.ok) print("Tree valid before delete");
else print("Tree invalid before delete");

print("Deleting 40...");
avl.erase(40);

print("Tree size after delete: " + avl.size);
res = check_avl(avl.root);

if (res.ok) {
    print("Tree is valid.");
} else {
    print("Tree is INVALID.");
    // Print 60's balance specifically
    let n60 = avl.find(60);
    if (n60) {
        print("Node 60 balance: " + n60.balance);
    }
}
