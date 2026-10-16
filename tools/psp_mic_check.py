#!/usr/bin/env python3
"""Find which key a WPA2 client used to sign 4-way handshake message 2.

The AP reports "invalid MIC in msg 2/4". This recomputes the MIC of the
client's message 2 under several candidate keys, so we can see *how* the
client's key differs from the right one (typo vs. raw passphrase used as
the key vs. wrong SSID, ...).

Inputs come from wpa_supplicant's AP debug log (log_level msgdump):
  --msg1  hex of the EAPOL frame the AP sent   ("Send EAPOL-Key msg - hexdump")
  --msg2  hex of the EAPOL frame the PSP sent  (the received EAPOL-Key hexdump)
Found 2026-10-10: Kommander's own WPA2 profile signs with the all-zero key.
Only standard-library Python. Run with: python3 -I psp_mic_check.py ...
"""
import argparse, hashlib, hmac

def frame(hexstr):
    return bytes.fromhex(hexstr.replace(" ", "").replace(":", ""))

# EAPOL-Key layout (802.11-2016 12.7.2), offsets from the start of the EAPOL header
NONCE, MIC, MIC_LEN = 17, 81, 16

def mac(s):
    return bytes.fromhex(s.replace(":", ""))

def prf512(key, label, data):
    out = b""
    for i in range(4):  # 4 * 20 bytes of SHA-1 >= 64 bytes
        out += hmac.new(key, label + b"\x00" + data + bytes([i]), hashlib.sha1).digest()
    return out[:64]

def mic_for(pmk, aa, spa, anonce, msg2):
    snonce = msg2[NONCE:NONCE + 32]
    data = min(aa, spa) + max(aa, spa) + min(anonce, snonce) + max(anonce, snonce)
    kck = prf512(pmk, b"Pairwise key expansion", data)[:16]
    zeroed = msg2[:MIC] + bytes(MIC_LEN) + msg2[MIC + MIC_LEN:]
    return hmac.new(kck, zeroed, hashlib.sha1).digest()[:MIC_LEN]

def pbkdf2(passphrase, ssid):
    return hashlib.pbkdf2_hmac("sha1", passphrase, ssid, 4096, 32)

def candidates(pw, ssid):
    """(label, PMK) pairs: the right key first, then plausible mistakes."""
    yield "correct: PBKDF2(passphrase, SSID)", pbkdf2(pw, ssid)
    yield "raw passphrase used as the key (zero-padded)", pw.ljust(32, b"\x00")[:32]
    yield "all-zero key (key field never filled in)", bytes(32)
    yield "passphrase + NUL terminator", pbkdf2(pw + b"\x00", ssid)
    yield "SSID + NUL terminator", pbkdf2(pw, ssid + b"\x00")
    yield "SSID padded to 32 bytes", pbkdf2(pw, ssid.ljust(32, b"\x00"))
    yield "empty SSID", pbkdf2(pw, b"")
    yield "SSID lowercased", pbkdf2(pw, ssid.lower())
    yield "SSID uppercased", pbkdf2(pw, ssid.upper())
    yield "passphrase uppercased", pbkdf2(pw.upper(), ssid)
    yield "passphrase padded to 63 bytes", pbkdf2(pw.ljust(63, b"\x00"), ssid)
    yield "passphrase padded to 64 bytes", pbkdf2(pw.ljust(64, b"\x00"), ssid)
    for n in range(1, len(pw)):
        yield f"passphrase cut to {n} chars", pbkdf2(pw[:n], ssid)
    yield "SHA-1 of passphrase as key", hashlib.sha1(pw).digest().ljust(32, b"\x00")
    yield "SHA-256 of passphrase as key", hashlib.sha256(pw).digest()

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--msg1", required=True); p.add_argument("--msg2", required=True)
    p.add_argument("--ap", required=True, help="AP MAC (hotspot)")
    p.add_argument("--sta", required=True, help="client MAC (PSP)")
    p.add_argument("--ssid", required=True); p.add_argument("--passphrase", required=True)
    a = p.parse_args()

    msg1, msg2 = frame(a.msg1), frame(a.msg2)
    anonce = msg1[NONCE:NONCE + 32]
    sent = msg2[MIC:MIC + MIC_LEN]
    print(f"ANonce {anonce.hex()}\nSNonce {msg2[NONCE:NONCE+32].hex()}\nPSP's MIC {sent.hex()}\n")
    hits, seen = 0, set()
    for label, pmk in candidates(a.passphrase.encode(), a.ssid.encode()):
        # Skip variants that give the same key as one already tried (for example,
        # HMAC pads its key with zeros, so a NUL-padded passphrase is the same key).
        if pmk in seen:
            continue
        seen.add(pmk)
        ok = hmac.compare_digest(mic_for(pmk, mac(a.ap), mac(a.sta), anonce, msg2), sent)
        if ok or label.startswith("correct"):
            print(("MATCH  " if ok else "no     ") + label)
        hits += ok
    if not hits:
        print("\nNo candidate matched: the PSP's key is none of these variants.")

if __name__ == "__main__":
    main()
