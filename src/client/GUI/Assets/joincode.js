// ---------------------------------------------------------------------------
// Join codes: a server address, port, room and password packed into one short
// string that can be shared with other people.
//
// Every field is turned into a sequence of symbols, each with its own radix
// (an IPv4 octet is a digit in base 256, a hostname character a digit in base
// 40, ...). The symbols are combined into one big integer in mixed radix,
// which is then written in Base58. This spends close to the information-
// theoretical minimum on each field, and because the last symbols end up in
// the most significant digits, trailing zeros cost nothing: the default port
// and an empty password don't make the code any longer.
//
// Symbol layout (version 1), least significant first:
//   version   radix 4
//   checksum  radix 65536  FNV-1a of the decoded fields, catches typos
//   host      radix 3 type (0 = IPv4: 4 x 256, 1 = IPv6: 8 x 65536, 2 = string)
//   room      string
//   port      radix 2 flag (0 = default port), else radix 65536
//   password  string
//
// A string is a radix-3 alphabet selector, its length (radix 32, where 31
// means "plus a radix-4096 extension"), then one symbol per character:
//   0: lowercase, digits and " -._" (40 symbols, 5.3 bits per character)
//   1: printable ASCII              (95 symbols, 6.6 bits per character)
//   2: UTF-8 bytes                  (256 symbols, the fallback)
//
// Version 0 codes are identical except for a radix-256 checksum. They are
// still accepted, but new codes always use the stronger version 1 checksum.
//
// This file is shared with tools/join-code-generator.html, which carries an
// identical copy between its JOINCODE BEGIN/END markers (checked by a test).
// ---------------------------------------------------------------------------

const JoinCode = (() => {
  const VERSION        = 1;
  const DEFAULT_PORT   = 24816;
  const BASE58         = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  const MAX_STRING_LEN = 31 + 4095;

  const ASCII = Array.from({ length: 95 }, (_, i) => String.fromCharCode(32 + i)).join("");
  const ALPHABETS = ["abcdefghijklmnopqrstuvwxyz0123456789 -._", ASCII];

  const HOST_IPV4 = 0, HOST_IPV6 = 1, HOST_STRING = 2;

  // --- Mixed-radix integer ---

  class Writer {
    constructor() { this.symbols = []; }
    put(value, radix) { this.symbols.push([BigInt(value), BigInt(radix)]); }
    toBigInt() {
      let n = 0n;
      for (let i = this.symbols.length - 1; i >= 0; i--) {
        const [value, radix] = this.symbols[i];
        n = n * radix + value;
      }
      return n;
    }
  }

  class Reader {
    constructor(n) { this.n = n; }
    get(radix) {
      const r = BigInt(radix);
      const value = this.n % r;
      this.n /= r;
      return Number(value);
    }
  }

  // --- Strings ---

  function putString(w, s) {
    const chars = Array.from(s);
    let mode = ALPHABETS.findIndex((a) => chars.every((c) => a.includes(c)));
    let values, radix;
    if (mode >= 0) {
      values = chars.map((c) => ALPHABETS[mode].indexOf(c));
      radix  = ALPHABETS[mode].length;
    } else {
      mode   = 2;
      values = Array.from(new TextEncoder().encode(s));
      radix  = 256;
    }
    if (values.length > MAX_STRING_LEN) throw new Error("Text is too long");

    w.put(mode, 3);
    w.put(Math.min(values.length, 31), 32);
    if (values.length >= 31) w.put(values.length - 31, 4096);
    values.forEach((v) => w.put(v, radix));
  }

  function getString(r) {
    const mode = r.get(3);
    let length = r.get(32);
    if (length === 31) length += r.get(4096);
    if (mode === 2) {
      const bytes = Uint8Array.from({ length }, () => r.get(256));
      return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    }
    const alphabet = ALPHABETS[mode];
    return Array.from({ length }, () => alphabet[r.get(alphabet.length)]).join("");
  }

  // --- Host addresses ---

  function parseIPv4(s) {
    const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(s);
    if (!m) return null;
    const octets = m.slice(1);
    // Leading zeros would not survive the round trip, so treat them as text
    if (octets.some((o) => Number(o) > 255 || (o.length > 1 && o[0] === "0"))) return null;
    return octets.map(Number);
  }

  function parseIPv6(s) {
    if (s.startsWith("[") && s.endsWith("]")) s = s.slice(1, -1);
    if (!/^[0-9a-fA-F:]+$/.test(s)) return null;
    const halves = s.split("::");
    if (halves.length > 2) return null;
    const split = (h) => (h === "" ? [] : h.split(":"));
    const head = split(halves[0]);
    const tail = halves.length === 2 ? split(halves[1]) : [];
    const missing = 8 - head.length - tail.length;
    if (halves.length === 2 ? missing < 1 : missing !== 0) return null;
    const groups = [...head, ...Array(halves.length === 2 ? missing : 0).fill("0"), ...tail];
    if (groups.some((g) => g.length < 1 || g.length > 4)) return null;
    return groups.map((g) => parseInt(g, 16));
  }

  // RFC 5952 text form: lowercase, longest run of two or more zero groups as "::"
  function formatIPv6(groups) {
    let bestStart = -1, bestLen = 1;
    for (let i = 0; i < 8; ) {
      if (groups[i] !== 0) { i++; continue; }
      let j = i;
      while (j < 8 && groups[j] === 0) j++;
      if (j - i > bestLen) { bestStart = i; bestLen = j - i; }
      i = j;
    }
    const hex = groups.map((g) => g.toString(16));
    if (bestStart < 0) return hex.join(":");
    return hex.slice(0, bestStart).join(":") + "::" + hex.slice(bestStart + bestLen).join(":");
  }

  // Returns { type, value, text }, where text is the canonical host string
  function parseHost(host) {
    const ipv4 = parseIPv4(host);
    if (ipv4) return { type: HOST_IPV4, value: ipv4, text: ipv4.join(".") };
    const ipv6 = parseIPv6(host);
    if (ipv6) return { type: HOST_IPV6, value: ipv6, text: formatIPv6(ipv6) };
    // DNS names are case-insensitive; lowercase keeps them in the small alphabet
    const text = /^[A-Za-z0-9.-]+$/.test(host) ? host.toLowerCase() : host;
    return { type: HOST_STRING, value: text, text };
  }

  // --- Checksum ---

  // Checksum radix per version: 8 bits in version 0, 16 bits from version 1 on
  const CHECKSUM_RADIX = [256, 65536];

  function checksum(version, host, port, room, password) {
    const bytes = new TextEncoder().encode([host, port, room, password].join("\n"));
    let h = 0x811c9dc5;
    for (const b of bytes) h = Math.imul(h ^ b, 0x01000193) >>> 0;
    if (version === 0) return (h ^ (h >>> 8) ^ (h >>> 16) ^ (h >>> 24)) & 0xff;
    return (h ^ (h >>> 16)) & 0xffff;
  }

  // --- Base58 ---

  function toBase58(n) {
    let s = "";
    do {
      s = BASE58[Number(n % 58n)] + s;
      n /= 58n;
    } while (n > 0n);
    return s;
  }

  function fromBase58(s) {
    let n = 0n;
    for (const c of s) {
      const digit = BASE58.indexOf(c);
      if (digit < 0) return null;
      n = n * 58n + BigInt(digit);
    }
    return n;
  }

  // --- Public API ---

  // { address, port, room, password } -> join code string. Throws on bad input.
  function encode({ address, port = DEFAULT_PORT, room, password = "" }) {
    address = String(address).trim();
    room    = String(room).trim();
    port    = Number(port);
    if (!address) throw new Error("Server address is required");
    if (!room) throw new Error("Room name is required");
    if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error("Port must be between 1 and 65535");

    const host = parseHost(address);
    const w = new Writer();
    w.put(VERSION, 4);
    w.put(checksum(VERSION, host.text, port, room, password), CHECKSUM_RADIX[VERSION]);

    w.put(host.type, 3);
    if (host.type === HOST_IPV4) host.value.forEach((o) => w.put(o, 256));
    else if (host.type === HOST_IPV6) host.value.forEach((g) => w.put(g, 65536));
    else putString(w, host.value);

    putString(w, room);

    w.put(port === DEFAULT_PORT ? 0 : 1, 2);
    if (port !== DEFAULT_PORT) w.put(port, 65536);

    putString(w, password);

    return toBase58(w.toBigInt());
  }

  // Join code string -> { address, port, room, password }. Throws if the code
  // is malformed, mistyped or from a newer version.
  function decode(code) {
    const invalid = () => new Error("This is not a valid join code");
    const n = fromBase58(String(code).replace(/\s+/g, ""));
    if (n === null) throw invalid();

    try {
      const r = new Reader(n);
      const version = r.get(4);
      if (version > VERSION) throw invalid();
      const check = r.get(CHECKSUM_RADIX[version]);

      let address;
      const hostType = r.get(3);
      if (hostType === HOST_IPV4) address = Array.from({ length: 4 }, () => r.get(256)).join(".");
      else if (hostType === HOST_IPV6) address = formatIPv6(Array.from({ length: 8 }, () => r.get(65536)));
      else address = getString(r);

      const room = getString(r);
      const port = r.get(2) === 0 ? DEFAULT_PORT : r.get(65536);
      const password = getString(r);

      // Every digit must be accounted for and the checksum must match
      if (r.n !== 0n || !address || !room || port < 1 ||
          check !== checksum(version, address, port, room, password)) {
        throw invalid();
      }
      return { address, port, room, password };
    } catch (err) {
      throw invalid();
    }
  }

  // The form a server address takes in a join code (lowercase DNS name,
  // compressed IPv6), so differently written addresses of one server compare equal
  function canonicalHost(address) {
    return parseHost(String(address).trim()).text;
  }

  return { encode, decode, canonicalHost, DEFAULT_PORT };
})();

if (typeof module !== "undefined") module.exports = JoinCode;
