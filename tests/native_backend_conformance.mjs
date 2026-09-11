// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
// Offline integration test. Arguments: compiled product-test executable, backend checkout.
import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {pathToFileURL} from "node:url";
import {join, isAbsolute} from "node:path";
const [binary, backend] = process.argv.slice(2);
assert.equal(process.argv.length,4);
assert.ok(isAbsolute(binary) && isAbsolute(backend));
const load = relative => import(pathToFileURL(join(backend,relative)));
const {validateMessage, canonical} = await load("src/protocol.mjs");
const {openStore, TireStore} = await load("src/store.mjs");
const bytes = execFileSync(binary,["--emit-native-conformance"],{encoding:"utf8",timeout:30000,maxBuffer:1024*1024}).trim().split("\n");
assert.equal(bytes.length,4);
const uid=JSON.parse(bytes[0]).unitSystemUid;
const db=openStore(":memory:");
try {
  const store=new TireStore(db,()=>({testSystemUid:uid,systemUids:[uid]})), kinds=new Set();
  for(const line of bytes) {
    const value=validateMessage(line).message;
    assert.equal(canonical(value),line);kinds.add(value.messageType);
    assert.equal(value.schemaVersion,2);assert.equal(value.serviceVersion,"21.0.0");
    assert.equal(Object.hasOwn(value,"serviceArtifactSha256"),false);
    const first=store.ingest(line),again=store.ingest(line);
    assert.equal(first.status,201);assert.equal(again.status,200);
    assert.equal(first.body.receiptId,again.body.receiptId);assert.equal(first.body.schemaVersion,1);
    const retained=db.prepare("SELECT canonical FROM messages WHERE kind=?").get(value.messageType);
    assert.equal(retained.canonical,line);
  }
  assert.equal(kinds.size,4);
  for(const category of ["assessments","events","advisories","functionStatus"]) {
    const page=store.query(uid,category);assert.equal(page.schemaVersion,2);
    assert.equal(page.items.length,1);assert.equal(page.items[0].message.schemaVersion,2);
  }
  console.log("PASS four native Tire message kinds: real C++ serialization, backend validation/storage, exact retry receipts and queries");
} finally {db.close();}
