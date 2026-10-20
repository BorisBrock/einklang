// Tests the join code codec (src/client/GUI/Assets/joincode.js) and checks
// that tools/join-code-generator.html carries an identical copy of it.
//
//   node tests/JoinCodeTest.js          run the tests
//   node tests/JoinCodeTest.js --sync   copy joincode.js into the generator

const assert = require("assert");
const fs     = require("fs");
const path   = require("path");

const root          = path.join(__dirname, "..");
const codecPath     = path.join(root, "src/client/GUI/Assets/joincode.js");
const generatorPath = path.join(root, "tools/join-code-generator.html");
const JoinCode      = require(codecPath);

const BEGIN = "// JOINCODE BEGIN\n";
const END   = "// JOINCODE END\n";

// --- Generator copy ---

const codec = fs.readFileSync(codecPath, "utf8").replace(/\r\n/g, "\n");
const html  = fs.readFileSync(generatorPath, "utf8").replace(/\r\n/g, "\n");
const begin = html.indexOf(BEGIN) + BEGIN.length;
const end   = html.indexOf(END);
assert(begin >= BEGIN.length && end >= begin, "JOINCODE markers missing in the generator");

if (process.argv.includes("--sync")) {
  fs.writeFileSync(generatorPath, html.slice(0, begin) + codec + html.slice(end));
  console.log("Synced joincode.js into " + path.relative(root, generatorPath));
  process.exit(0);
}

assert.strictEqual(html.slice(begin, end), codec,
  "tools/join-code-generator.html is out of date, run: node tests/JoinCodeTest.js --sync");

// --- Round trips ---

const cases = [
  [{ address: "192.168.1.20", room: "lobby" },
   { address: "192.168.1.20", port: 24816, room: "lobby", password: "" }],
  [{ address: "MyServer.DuckDNS.org", port: 9999, room: "Raid Night", password: "s3cr3t!" },
   { address: "myserver.duckdns.org", port: 9999, room: "Raid Night", password: "s3cr3t!" }],
  [{ address: "[2001:DB8:0:0:0::1]", port: "65535", room: "Größe 🎮", password: "pässwörd " },
   { address: "2001:db8::1", port: 65535, room: "Größe 🎮", password: "pässwörd " }],
  [{ address: "010.1.1.1", port: 1, room: "x".repeat(200) },
   { address: "010.1.1.1", port: 1, room: "x".repeat(200), password: "" }],
  [{ address: "0.0.0.0", room: "a" },
   { address: "0.0.0.0", port: 24816, room: "a", password: "" }],
];
for (const [input, expected] of cases) {
  const code = JoinCode.encode(input);
  assert.match(code, /^[1-9A-HJ-NP-Za-km-z]+$/);
  assert.deepStrictEqual(JoinCode.decode(code), expected);
  assert.deepStrictEqual(JoinCode.decode(" " + code.replace(/(.{5})/g, "$1\n") + " "), expected);
}

// The common case should stay short
assert(JoinCode.encode({ address: "192.168.1.20", room: "lobby" }).length <= 15);

// Version 0 codes (8-bit checksum) handed out by older clients still work
assert.deepStrictEqual(JoinCode.decode("vkua27kaRHNgo"),
  { address: "192.168.1.20", port: 24816, room: "lobby", password: "" });

// --- Canonical host (used to match rooms to an already known server) ---

assert.strictEqual(JoinCode.canonicalHost(" MyServer.DE "), "myserver.de");
assert.strictEqual(JoinCode.canonicalHost("[0:0:0:0:0:0:0:1]"), "::1");
assert.strictEqual(JoinCode.canonicalHost("192.168.1.20"), "192.168.1.20");

// --- Invalid input ---

assert.throws(() => JoinCode.encode({ address: "", room: "a" }));
assert.throws(() => JoinCode.encode({ address: "a", room: " " }));
assert.throws(() => JoinCode.encode({ address: "a", room: "a", port: 0 }));
assert.throws(() => JoinCode.encode({ address: "a", room: "a", port: 65536 }));
for (const bad of ["", "hello world", "0OIl", "1"]) assert.throws(() => JoinCode.decode(bad));

// Single-character typos must be detected (the 16-bit checksum lets about
// 1 in 65536 through, the structural checks catch most of those)
const ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const sample = JoinCode.encode({ address: "game.example.com", port: 5000, room: "Squad", password: "pw" });
let typos = 0, undetected = 0;
for (let i = 0; i < sample.length; i++) {
  for (const c of ALPHABET) {
    if (c === sample[i]) continue;
    typos++;
    try { JoinCode.decode(sample.slice(0, i) + c + sample.slice(i + 1)); undetected++; } catch {}
  }
}
assert(undetected / typos < 0.001, undetected + " of " + typos + " typos went undetected");

console.log("JoinCodeTest passed");
