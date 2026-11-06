"""soundpack.py - the sound bake (S9, S10): extracted sound tags re-encoded
smaller for the PSP, before invader-build builds the map from them.

The disc stores effects and dialogue at 22,050 Hz (some stereo) and music
and some ambience at 44,100 Hz stereo, all Xbox ADPCM. The port mixes at
22,050 Hz (psp_dsound.c), so everything above 11 kHz is lost on the PSP
anyway, and an effect played in 3D is mixed as mono. streaming.md S9
(2026-10-11: "a reduction in resource usage with near zero quality cost"):

    s9    every sound at 22,050 Hz at most; effects mono; music stereo
    s9full  s9, the music left as the disc has it (44.1 kHz stereo), for
          the mixer's full-rate path (MUSADPCM, keep word musadpcm)
    s9mono  every sound at 22,050 Hz at most and mono, the music and the
          ambience included: the PSP's speakers are the target, and a 3D
          effect is mixed mono and panned anyway (Sheepdog01 2026-11-06 17:07:
          nothing that gameplay doesn't need in stereo stays stereo)
    s10   s9, and footsteps, impacts and casings at 11,025 Hz (S10: a
          listening A/B first)

The codec stays Xbox ADPCM, so the mixer is untouched: each permutation is
decoded, downmixed and low-passed to half rate as its profile says, and
encoded again. Blocks are encoded independently (each header carries the
true sample before the block and a step index fitted to the block), which
lets numpy encode every block of a permutation at once; a second
generation costs a little (median 29 dB SNR in audio.md's prototype).

S10's 11 kHz isn't a rate the sound tag can say (22,050 or 44,100 only), so
it isn't applied yet: the tag needs the rate per permutation, which the
runtime doesn't read (audio.md 3b). s10 is accepted and reported as s9.

    python3 soundpack.py TAGS_DIR PROFILE [CACHE_DIR] [--music-stream PACK MUSICENC | --music-adpcm PACK]

With CACHE_DIR, each tag's re-encode is kept there by a hash of the tag's
bytes, the profile, this file and numpy's version, and read back by a later
bake instead of made again (the same bytes either way).

--music-stream (the music path, docs/plan_music_stream.md) also writes PACK:
every music chain at 44,100 Hz, before S9 halves it, encoded as Vorbis by the
bundle's musicenc, with where each permutation starts in its chain. The tags
are rewritten exactly as without it, so the map is the same. PROFILE may then
be "none": the tags are left as they are and only the pack is written. The
pack is made from the disc's sounds: it stays in the bake's output folder
like the map, and only its counts are printed. The pack is gathered while
each tag is read, so with it CACHE_DIR is not used.

Reads Invader's tag definitions from HALO_PSP_INVADER_DEFINITIONS. Writes
the sound tags in place and prints a summary (counts and bytes: no tag
data)."""
import glob
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

STEPS = np.array([
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767], np.int32)
INDEX_STEP = np.array([-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8], np.int32)
BLOCK_SAMPLES = 64
BLOCK_BYTES = 36           # per channel
FORMAT_XBOX_ADPCM = 1
RATE_22050, RATE_44100 = 0, 1
MONO, STEREO = 0, 1
CLASS_MUSIC = 32
# S10's classes (kept for when the rate can be per permutation)
CLASSES_NOISY = {13, 14, 15, 18}   # object, particle and slow particle impacts; unit footsteps

# ---------------------------------------------------------------- the codec


def _expand(nibble, predictor, index):
    """the decoder's step (psp_dsound.c ima_expand), on arrays"""
    step = STEPS[index]
    diff = step >> 3
    diff = diff + np.where(nibble & 1, step >> 2, 0)
    diff = diff + np.where(nibble & 2, step >> 1, 0)
    diff = diff + np.where(nibble & 4, step, 0)
    diff = np.where(nibble & 8, -diff, diff)
    predictor = np.clip(predictor + diff, -32768, 32767)
    index = np.clip(index + INDEX_STEP[nibble], 0, 88)
    return predictor, index


def decode(data, channels):
    """Xbox ADPCM bytes -> int16 array (samples, channels), as psp_dsound.c
    decodes: per block, a 4-byte header a channel (the block's first sample,
    step index), then 8 groups of 4 bytes a channel, eight nibbles each, low
    first, coding the 63 samples after the header's (the 64th pads)"""
    block_bytes = BLOCK_BYTES * channels
    blocks = len(data) // block_bytes
    if blocks == 0:
        return np.zeros((0, channels), np.int16)
    raw = np.frombuffer(data[:blocks * block_bytes], np.uint8).reshape(blocks, block_bytes)
    out = np.zeros((blocks, BLOCK_SAMPLES, channels), np.int16)
    for c in range(channels):
        header = raw[:, 4 * c:4 * c + 4].astype(np.int32)
        predictor = (header[:, 0] | (header[:, 1] << 8)).astype(np.int32)
        predictor = np.where(predictor >= 32768, predictor - 65536, predictor)
        index = np.minimum(header[:, 2], 88)
        out[:, 0, c] = predictor
        for group in range(8):
            at = 4 * channels + (group * channels + c) * 4
            for byte in range(4):
                b = raw[:, at + byte].astype(np.int32)
                for half, nibble in enumerate((b & 15, b >> 4)):
                    sample = group * 8 + byte * 2 + half + 1
                    if sample >= BLOCK_SAMPLES:
                        break
                    predictor, index = _expand(nibble, predictor, index)
                    out[:, sample, c] = predictor
    return out.reshape(blocks * BLOCK_SAMPLES, channels)


def encode(pcm):
    """int16 array (samples, channels) -> Xbox ADPCM bytes, whole blocks
    (the last padded with silence). Each block on its own: the header holds
    the block's first sample exactly and a step index fitted to its first
    differences; nibbles code the 63 samples after it, each the one whose
    decoded value is nearest (all sixteen tried); the 64th nibble pads (0),
    as the Xbox's own encoder leaves it"""
    samples, channels = pcm.shape
    blocks = max(1, (samples + BLOCK_SAMPLES - 1) // BLOCK_SAMPLES)
    padded = np.zeros((blocks * BLOCK_SAMPLES, channels), np.int32)
    padded[:samples] = pcm
    x = padded.reshape(blocks, BLOCK_SAMPLES, channels)
    out = np.zeros((blocks, BLOCK_BYTES * channels), np.uint8)
    nibbles_all = np.arange(16, dtype=np.int32)
    for c in range(channels):
        signal = x[:, :, c]
        first = np.abs(np.diff(signal[:, :5], axis=1)).mean(1)
        index = np.clip(np.searchsorted(STEPS, (first * 2).astype(np.int32)), 0, 88).astype(np.int32)
        predictor = np.clip(signal[:, 0], -32768, 32767).astype(np.int32)
        out[:, 4 * c] = (predictor & 0xFF).astype(np.uint8)
        out[:, 4 * c + 1] = ((predictor >> 8) & 0xFF).astype(np.uint8)
        out[:, 4 * c + 2] = index.astype(np.uint8)
        codes = np.zeros((blocks, BLOCK_SAMPLES), np.int32)
        for k in range(BLOCK_SAMPLES - 1):
            # every nibble's outcome, the nearest kept
            p = np.repeat(predictor[:, None], 16, 1)
            i = np.repeat(index[:, None], 16, 1)
            n = np.broadcast_to(nibbles_all, p.shape)
            p2, i2 = _expand(n, p, i)
            best = np.abs(p2 - signal[:, k + 1:k + 2]).argmin(1)
            rows = np.arange(blocks)
            codes[:, k] = best
            predictor = p2[rows, best]
            index = i2[rows, best]
        for group in range(8):
            at = 4 * channels + (group * channels + c) * 4
            for byte in range(4):
                lo = codes[:, group * 8 + byte * 2]
                hi = codes[:, group * 8 + byte * 2 + 1]
                out[:, at + byte] = (lo | (hi << 4)).astype(np.uint8)
    return out.tobytes()


def half_rate(pcm):
    """44,100 -> 22,050 Hz: a windowed-sinc low-pass at a little under the
    new Nyquist, then every second sample"""
    taps = 63
    n = np.arange(taps) - (taps - 1) / 2
    h = np.sinc(2 * 0.225 * n) * np.hamming(taps)    # cut at 0.225 of the old rate: 9.9 kHz
    h /= h.sum()
    out = np.empty(((pcm.shape[0] + 1) // 2, pcm.shape[1]), np.float64)
    for c in range(pcm.shape[1]):
        filtered = np.convolve(pcm[:, c].astype(np.float64), h, mode='same')
        out[:, c] = filtered[::2]
    return np.clip(np.round(out), -32768, 32767).astype(np.int16)


def mono(pcm):
    if pcm.shape[1] == 1:
        return pcm
    return np.clip(np.round(pcm.astype(np.float64).mean(1)), -32768, 32767).astype(np.int16)[:, None]


def snr(reference, test):
    n = min(len(reference), len(test))
    if n == 0:
        return float('inf')
    r = reference[:n].astype(np.float64)
    e = r - test[:n].astype(np.float64)
    power, noise = (r * r).mean(), (e * e).mean()
    return float('inf') if noise == 0 else 10 * np.log10(max(power, 1e-9) / noise)

# ---------------------------------------------------------------- the tags

PRIMITIVES = {
    'int8': 1, 'uint8': 1, 'int16': 2, 'uint16': 2, 'int32': 4, 'uint32': 4,
    'float': 4, 'Angle': 4, 'Fraction': 4, 'Index': 2, 'Pointer': 4, 'TagID': 4,
    'TagFourCC': 4, 'ColorARGBInt': 4, 'Point2DInt': 4, 'Rectangle2D': 8,
    'Point2D': 8, 'Vector2D': 8, 'Euler2D': 8, 'Plane2D': 12,
    'Point3D': 12, 'Vector3D': 12, 'Euler3D': 12, 'ColorRGB': 12,
    'Plane3D': 16, 'Quaternion': 16, 'ColorARGB': 16, 'Matrix': 36,
    'TagString': 32, 'TagDependency': 16, 'TagReflexive': 12, 'TagDataOffset': 20,
    'ScenarioScriptNodeValue': 4,
}


class Definitions:
    """Invader's tag definitions: struct layouts (as tools/hek_census.py)"""

    def __init__(self, folder):
        self.structs, self.enums, self.bitfields = {}, {}, {}
        for path in glob.glob(os.path.join(folder, '*.json')):
            for item in json.load(open(path, encoding='utf-8')):
                kind = item.get('type')
                if kind == 'struct':
                    self.structs[item['name']] = item
                elif kind == 'enum':
                    self.enums[item['name']] = item
                elif kind == 'bitfield':
                    self.bitfields[item['name']] = item
        self.cache = {}

    def field_size(self, field):
        kind = field['type']
        if kind == 'pad':
            return field['size']
        if kind in PRIMITIVES:
            size = PRIMITIVES[kind]
        elif kind in self.enums:
            size = 2
        elif kind in self.bitfields:
            size = self.bitfields[kind]['width'] // 8
        elif kind in self.structs:
            size = self.size(kind)
        else:
            raise KeyError('unknown field type %s' % kind)
        if field.get('bounds'):
            size *= 2
        return size * field.get('count', 1)

    def layout(self, name):
        """[(offset, field)], inherited fields first"""
        if name in self.cache:
            return self.cache[name]
        s = self.structs[name]
        fields, offset = [], 0
        if 'inherits' in s:
            fields = list(self.layout(s['inherits']))
            offset = self.size(s['inherits'])
        for field in s['fields']:
            fields.append((offset, field))
            offset += self.field_size(field)
        if 'size' in s and s['size'] != offset:
            raise ValueError('%s: fields sum to %d, the definition says %d' % (name, offset, s['size']))
        self.cache[name] = fields
        self.cache[name + '#size'] = offset
        return fields

    def size(self, name):
        if name + '#size' not in self.cache:
            self.layout(name)
        return self.cache[name + '#size']

    def offset(self, name, field_name):
        for offset, field in self.layout(name):
            if field.get('name') == field_name:
                return offset
        raise KeyError('%s has no field %s' % (name, field_name))


class Rewriter:
    """a HEK tag file re-emitted in order (the 64-byte header, the main
    struct, then each struct's children after its array: dependency paths,
    data blobs, child arrays, in field order). replace(path, element) gives
    a data blob's new bytes, or None to keep it; each struct is handed to
    patch(name, bytearray, element path) to change its fields"""

    def __init__(self, definitions, data):
        self.d = definitions
        self.data = data
        self.at = 64

    def run(self, top, replace, patch):
        out = bytearray(self.data[:64])
        size = self.d.size(top)
        main = bytearray(self.data[64:64 + size])
        self.at = 64 + size
        at_out = len(out)
        out += main
        self._children(top, at_out, out, (), replace, patch)
        patch(top, out, at_out, ())
        if self.at != len(self.data):
            raise ValueError('%d bytes after the tag (read %d of %d)' % (len(self.data) - self.at, self.at, len(self.data)))
        return bytes(out)

    def _children(self, name, base, out, path, replace, patch):
        for offset, field in self.d.layout(name):
            kind = field['type']
            at = base + offset
            if kind == 'TagDependency':
                length = struct.unpack_from('>I', out, at + 8)[0]
                if length:
                    out += self.data[self.at:self.at + length + 1]
                    self.at += length + 1
            elif kind == 'TagDataOffset':
                size = struct.unpack_from('>I', out, at)[0]
                blob = self.data[self.at:self.at + size]
                self.at += size
                new = replace(name, field.get('name'), path, blob)
                if new is not None:
                    blob = new
                    struct.pack_into('>I', out, at, len(blob))
                out += blob
            elif kind == 'TagReflexive':
                count = struct.unpack_from('>I', out, at)[0]
                child = field['struct']
                child_size = self.d.size(child)
                start = len(out)
                out += self.data[self.at:self.at + count * child_size]
                self.at += count * child_size
                for i in range(count):
                    element = path + ((field.get('name'), i),)
                    self._children(child, start + i * child_size, out, element, replace, patch)
                    patch(child, out, start + i * child_size, element)
            elif kind in self.d.structs:
                for i in range(field.get('count', 1)):
                    self._children(kind, at + i * self.d.size(kind), out, path, replace, patch)

# ---------------------------------------------------------------- one sound


# ------------------------------------------------------- the music stream

MUSIC_RATE = 44100
MUSIC_QUALITY = '0.3'      # libvorbis VBR quality 3: about 112 kbps for stereo music
PACK_MAGIC = b'PMUS'
PACK_VERSION = 1
ADPCM_PACK_MAGIC = b'PMAD'
ADPCM_PACK_VERSION = 1


def tag_hash(name):
    """FNV-1a over the tag's path as the game names it (lower case, back
    slashes, no extension): the game hashes the same string for its lookup"""
    h = 0x811C9DC5
    for byte in name.lower().replace('/', '\\').encode('ascii', 'replace'):
        h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
    return h


class MusicPack:
    """the music chains of one map's sound tags, and the pack made of them"""

    def __init__(self, encoder, work):
        self.encoder, self.work = encoder, work
        self.setups = []           # distinct header blobs (all the same when every chain is stereo 44.1 kHz)
        self.chains = []           # (setup, [(bytes, granule)], frames)
        self.entries = []          # (hash, pitch, permutation, chain, start, frames, first packet)
        self.skipped = 0           # music the pack leaves to the cache: not 44,100 Hz on the disc

    def encode(self, pcm):
        """int16 (frames, channels) at 44.1 kHz -> (setup index, packets, frames).
        Mono music is encoded as two equal channels, so one decoder setup
        serves every stream (an equal side costs Vorbis next to nothing)"""
        if pcm.shape[1] == 1:
            pcm = np.repeat(pcm, 2, 1)
        raw = os.path.join(self.work, 'music.raw')
        out = os.path.join(self.work, 'music.pkt')
        pcm.astype('<i2').tofile(raw)
        subprocess.run([self.encoder, str(MUSIC_RATE), '2', MUSIC_QUALITY, raw, out], check=True)
        blob = open(out, 'rb').read()
        records, at = [], 0
        while at < len(blob):
            size, granule = struct.unpack_from('<Iq', blob, at)
            records.append((blob[at + 12:at + 12 + size], granule))
            at += 12 + size
        if len(records) < 3:
            raise ValueError('musicenc wrote %d packets' % len(records))
        headers = b''.join(struct.pack('<I', len(p)) + p for p, _ in records[:3])
        if headers not in self.setups:
            self.setups.append(headers)
        return self.setups.index(headers), records[3:], len(pcm)

    def add_chain(self, name, pitch, chain, pcm, starts):
        """one chain's PCM at 44.1 kHz; starts: (permutation, first frame, frames) at 44.1 kHz"""
        setup, packets, frames = self.encode(pcm)
        index = len(self.chains)
        self.chains.append((setup, packets, frames))
        granules = [g for _, g in packets]
        for permutation, start, length in starts:
            # the packet whose decoded frames hold the start: decoded after
            # the one before it (Vorbis overlaps each packet with the last)
            first = next((k for k, g in enumerate(granules) if g > start), len(granules) - 1)
            self.entries.append((tag_hash(name), pitch, permutation, index, start, length, first))

    def write(self, path):
        """the pack, little-endian: a 64-byte header, the setups, the entries
        sorted by (tag hash, pitch range, permutation), the chains' table,
        then each chain's packet index and packets, a chain's packets
        contiguous so the stick reads a chain front to back"""
        entries = sorted(self.entries)
        head = 64
        setups = b''.join(struct.pack('<I', len(s)) + s + b'\0' * (-len(s) % 4) for s in self.setups)
        entries_at = head + len(setups)
        entry_bytes = b''.join(struct.pack('<IHHIIII', *e) for e in entries)
        chains_at = entries_at + len(entry_bytes)
        data_at = chains_at + 24 * len(self.chains)
        table, data = b'', b''
        for setup, packets, frames in self.chains:
            index = b''.join(struct.pack('<II', len(p), g) for p, g in packets)
            body = b''.join(p for p, _ in packets)
            index_at = data_at + len(data)
            packets_at = index_at + len(index)
            table += struct.pack('<IIIIII', setup, len(packets), frames, index_at, packets_at, len(body))
            data += index + body + b'\0' * (-(len(index) + len(body)) % 64)
        # the pack's match with the map: a hash over its keys, which the game
        # works out again from the map's music tags before it trusts the pack
        keys = 0x811C9DC5
        for e in entries:
            for byte in struct.pack('<IHH', *e[:3]):
                keys = ((keys ^ byte) * 0x01000193) & 0xFFFFFFFF
        header = struct.pack('<4sIIIIIIII', PACK_MAGIC, PACK_VERSION, len(self.setups), len(entries),
                             len(self.chains), entries_at, chains_at, keys, MUSIC_RATE)
        with open(path, 'wb') as f:
            f.write(header + b'\0' * (head - len(header)) + setups + entry_bytes + table + data)
        return head + len(setups) + len(entry_bytes) + len(table) + len(data)


class MusicAdpcmPack:
    """MUSADPCM (Sheepdog01 2026-10-30 19:51): the music chains of one map's sound
    tags as the disc has them, 44.1 kHz Xbox ADPCM, copied unchanged. An Xbox
    ADPCM block carries its own predictor and step, so a stream may start at
    any block: a chain is its permutations' blobs back to back, and a frame's
    byte offset is (frame // 64) * 36 * channels. The entries and the key hash
    are MusicPack's, so the game matches the pack to the map the same way"""
    takes_blobs = True

    def __init__(self):
        self.setups = []           # none: kept so the summary reads as MusicPack's
        self.chains = []           # (channels, body, frames)
        self.entries = []          # (hash, pitch, permutation, chain, start, frames)
        self.skipped = 0           # music the pack leaves to the cache: not 44,100 Hz on the disc
        self.unaligned = 0         # chains whose blobs aren't whole blocks (left out)

    def add_chain(self, name, pitch, chain, pcm, starts, blobs=None, channels=2):
        """blobs: the chain's permutations' samples as the disc has them;
        pcm: their decode joined (MusicPack's input), to check against"""
        block = BLOCK_BYTES * channels
        if not blobs or any(len(b) % block for b in blobs):
            self.unaligned += 1
            return
        body = b''.join(blobs)
        # the property the pack stands on: the body decoded whole is the
        # permutations decoded one by one and joined
        if len(decode(body, channels)) != len(pcm):
            self.unaligned += 1
            return
        index = len(self.chains)
        self.chains.append((channels, body, len(pcm)))
        for permutation, start, length in starts:
            self.entries.append((tag_hash(name), pitch, permutation, index, start, length))

    def write(self, path):
        """the pack, little-endian: a 64-byte header, the entries sorted by
        (tag hash, pitch range, permutation), the chains' table, then each
        chain's blocks, 64-byte aligned"""
        entries = sorted(self.entries)
        head = 64
        entries_at = head
        entry_bytes = b''.join(struct.pack('<IHHIII', *e) for e in entries)
        chains_at = entries_at + len(entry_bytes)
        data_at = chains_at + 16 * len(self.chains)
        data_at += -data_at % 64
        table, data = b'', b''
        for channels, body, frames in self.chains:
            table += struct.pack('<IIII', channels, frames, data_at + len(data), len(body))
            data += body + b'\0' * (-len(body) % 64)
        keys = 0x811C9DC5
        for e in entries:
            for byte in struct.pack('<IHH', *e[:3]):
                keys = ((keys ^ byte) * 0x01000193) & 0xFFFFFFFF
        header = struct.pack('<4sIIIIIII', ADPCM_PACK_MAGIC, ADPCM_PACK_VERSION, len(entries), len(self.chains),
                             entries_at, chains_at, keys, MUSIC_RATE)
        pad = data_at - chains_at - len(table)
        with open(path, 'wb') as f:
            f.write(header + b'\0' * (head - len(header)) + entry_bytes + table + b'\0' * pad + data)
        return data_at + len(data)


def music_blobs(music, blobs, pitch, chain, channels):
    """the disc's samples of a chain, for a pack that keeps them (MUSADPCM)"""
    if not getattr(music, 'takes_blobs', False):
        return {}
    return {'blobs': [blobs[(pitch, j)] for j in chain], 'channels': channels}


def bake_sound(d, data, profile, stats, music=None, name=''):
    """the sound tag's new bytes, or None to leave it. music (a MusicPack)
    also takes the tag's music chains at 44.1 kHz"""
    sound = 'Sound'
    o_class = 64 + d.offset(sound, 'sound class')
    o_rate = 64 + d.offset(sound, 'sample rate')
    o_channels = 64 + d.offset(sound, 'channel count')
    o_format = 64 + d.offset(sound, 'format')
    o_longest = 64 + d.offset(sound, 'longest permutation length')
    sound_class, = struct.unpack_from('>H', data, o_class)
    rate, = struct.unpack_from('>H', data, o_rate)
    channels_enum, = struct.unpack_from('>H', data, o_channels)
    fmt, = struct.unpack_from('>H', data, o_format)
    if fmt != FORMAT_XBOX_ADPCM:
        stats['other format'] += 1
        return None
    channels = 2 if channels_enum == STEREO else 1
    keep_stereo = sound_class == CLASS_MUSIC and profile != 's9mono'
    rewrite = profile != 'none'
    to_mono = rewrite and channels == 2 and not keep_stereo
    # s9full (MUSADPCM): the music as the disc has it, 44.1 kHz stereo, for
    # the mixer's full-rate path (keep word musadpcm)
    music_full = profile == 's9full' and sound_class == CLASS_MUSIC
    to_half = rewrite and rate == RATE_44100 and not music_full
    streamed = music is not None and sound_class == CLASS_MUSIC
    if streamed and rate != RATE_44100:
        music.skipped += 1
        streamed = False
    if not to_mono and not to_half and not streamed:
        stats['kept'] += 1
        return None
    p_format = d.offset('SoundPermutation', 'format')
    p_buffer = d.offset('SoundPermutation', 'buffer size')
    p_next = d.offset('SoundPermutation', 'next permutation index')
    stats['changed'] += rewrite
    stats['mono'] += to_mono
    stats['half rate'] += to_half

    # first pass: each permutation's samples and its next, by (pitch range,
    # permutation). A long sound is a chain of permutations played back to
    # back (next permutation index), so a chain is one signal: encoded
    # chunk by chunk, each chunk's padded last block would put a moment of
    # silence at every join
    blobs, nexts = {}, {}

    def collect(name, field, path, blob):
        if name == 'SoundPermutation' and field == 'samples':
            blobs[(path[0][1], path[1][1])] = blob
        return None

    def note(name, out, at, path):
        if name == 'SoundPermutation':
            nexts[(path[0][1], path[1][1])] = struct.unpack_from('>h', out, at + p_next)[0]

    Rewriter(d, data).run(sound, collect, note)
    new_blobs = {}
    for key in sorted(blobs):
        pitch, index = key
        if key in new_blobs or any(nexts.get((pitch, j)) == index for (q, j) in blobs if q == pitch):
            continue   # done, or a chain's later chunk (its head does the chain)
        chain, seen = [], set()
        at = index
        while at is not None and at >= 0 and (pitch, at) in blobs and at not in seen:
            seen.add(at)
            chain.append(at)
            at = nexts.get((pitch, at))
        pieces = [decode(blobs[(pitch, j)], channels) for j in chain]
        lengths = [len(piece) for piece in pieces]
        pcm = np.concatenate(pieces) if pieces else np.zeros((0, channels), np.int16)
        full = pcm
        if not rewrite:
            # the pack only: each chunk as long as the disc's
            if streamed and len(pcm):
                starts = np.cumsum([0] + lengths)
                music.add_chain(name, pitch, chain, full,
                                [(j, int(starts[n]), int(lengths[n])) for n, j in enumerate(chain)],
                                **music_blobs(music, blobs, pitch, chain, channels))
            continue
        if to_mono:
            pcm = mono(pcm)
        if to_half:
            pcm = half_rate(pcm)
        encoded = encode(pcm) if len(pcm) else b''
        block = BLOCK_BYTES * pcm.shape[1]
        total_blocks = len(encoded) // block
        if len(stats['snr']) < 400 and len(pcm):
            # the re-encode's own loss: what was encoded against what decodes
            stats['snr'].append(snr(pcm[:, 0], decode(encoded, pcm.shape[1])[:, 0]))
        # the chunks keep their share of the chain, in whole blocks
        whole = sum(lengths) or 1
        cut, done = 0, 0
        starts = []
        # a chunk's frames in the stream: S9 halves music, so frame f of the
        # map's chain is frame 2f at 44.1 kHz (whole 64-frame blocks there)
        scale = 2 if to_half else 1
        for n, (j, length) in enumerate(zip(chain, lengths)):
            done += length
            end_block = total_blocks if n == len(chain) - 1 else int(round(total_blocks * done / whole))
            new_blobs[(pitch, j)] = encoded[cut * block:end_block * block]
            starts.append((j, cut * BLOCK_SAMPLES * scale, (end_block - cut) * BLOCK_SAMPLES * scale))
            cut = end_block
        if streamed and len(full):
            music.add_chain(name, pitch, chain, full, starts, **music_blobs(music, blobs, pitch, chain, channels))
        for j in chain:
            stats['bytes before'] += len(blobs[(pitch, j)])
            stats['bytes after'] += len(new_blobs[(pitch, j)])

    def replace(name, field, path, blob):
        if name != 'SoundPermutation' or field != 'samples':
            return None
        return new_blobs.get((path[0][1], path[1][1]))

    def patch(name, out, at, path):
        if name == 'SoundPermutation':
            fmt_here, = struct.unpack_from('>H', out, at + p_format)
            if fmt_here == FORMAT_XBOX_ADPCM:
                struct.pack_into('>I', out, at + p_buffer, 0)   # invader-build sets 0 for ADPCM
        elif name == sound:
            if to_half:
                struct.pack_into('>H', out, o_rate, RATE_22050)
            if to_mono:
                struct.pack_into('>H', out, o_channels, MONO)
            struct.pack_into('>I', out, o_longest, 0)            # invader-build works it out again

    if not rewrite:
        return None
    return Rewriter(d, data).run(sound, replace, patch)


with open(os.path.abspath(__file__), 'rb') as _f:
    CODE = hashlib.sha256(_f.read() + np.__version__.encode()).hexdigest()
del _f


def cached_bake(d, data, profile, stats, cache):
    """bake_sound(), or what an earlier bake kept for the same input: the
    new bytes (or none) and what it added to the summary's counts"""
    if not cache:
        return bake_sound(d, data, profile, stats)
    key = hashlib.sha256(CODE.encode() + profile.encode() + data).hexdigest()
    path = os.path.join(cache, key)
    try:
        with open(path + '.json', encoding='utf-8') as f:
            kept = json.load(f)
        new = None
        if kept['changed']:
            with open(path + '.bin', 'rb') as f:
                new = f.read()
        for k, v in kept['stats'].items():
            stats[k] = stats[k] + v
        return new
    except (OSError, ValueError, KeyError):
        pass
    before = {k: (list(v) if isinstance(v, list) else v) for k, v in stats.items()}
    new = bake_sound(d, data, profile, stats)
    delta = {k: (stats[k][len(before[k]):] if isinstance(v, list) else stats[k] - before[k])
             for k, v in before.items()}
    delta['snr'] = [float(x) for x in delta['snr']]
    try:   # written whole, then renamed, so maps baking side by side read all or nothing
        if new is not None:
            with open(path + '.bin.tmp%d' % os.getpid(), 'wb') as f:
                f.write(new)
            os.replace(path + '.bin.tmp%d' % os.getpid(), path + '.bin')
        with open(path + '.json.tmp%d' % os.getpid(), 'w', encoding='utf-8') as f:
            json.dump({'changed': new is not None, 'stats': delta}, f)
        os.replace(path + '.json.tmp%d' % os.getpid(), path + '.json')
    except OSError:
        pass
    return new


def main():
    args = sys.argv[1:]
    pack_path = encoder = None
    adpcm = False
    if '--music-stream' in args:
        i = args.index('--music-stream')
        if len(args) < i + 3:
            sys.exit(__doc__)
        pack_path, encoder = args[i + 1], args[i + 2]
        args = args[:i] + args[i + 3:]
    elif '--music-adpcm' in args:
        i = args.index('--music-adpcm')
        if len(args) < i + 2:
            sys.exit(__doc__)
        pack_path, adpcm = args[i + 1], True
        args = args[:i] + args[i + 2:]
    if len(args) not in (2, 3) or args[1] not in ('s9', 's9full', 's9mono', 's10') + (('none',) if pack_path else ()):
        sys.exit(__doc__)
    folder, profile = args[0], args[1]
    cache = args[2] if len(args) == 3 and not pack_path else ''
    work = tempfile.mkdtemp(prefix='music-', dir=os.path.dirname(os.path.abspath(folder))) if pack_path and not adpcm else None
    music = MusicAdpcmPack() if adpcm else MusicPack(encoder, work) if pack_path else None
    d = Definitions(os.environ['HALO_PSP_INVADER_DEFINITIONS'])
    stats = {'changed': 0, 'kept': 0, 'other format': 0, 'mono': 0, 'half rate': 0, 'failed': 0,
             'bytes before': 0, 'bytes after': 0, 'snr': []}
    paths = sorted(glob.glob(os.path.join(folder, '**', '*.sound'), recursive=True))
    for path in paths:
        data = open(path, 'rb').read()
        try:
            if music:
                name = os.path.splitext(os.path.relpath(path, folder))[0].replace(os.sep, '\\')
                new = bake_sound(d, data, profile, stats, music, name)
            else:
                new = cached_bake(d, data, profile, stats, cache)
        except Exception as e:  # a tag this can't read is left as it was
            stats['failed'] += 1
            print('soundpack: %s left as it was: %s' % (os.path.relpath(path, folder), e), file=sys.stderr)
            continue
        if new is not None:
            open(path, 'wb').write(new)
    before, after = stats['bytes before'], stats['bytes after']
    finite = [v for v in stats['snr'] if np.isfinite(v)]
    print('soundpack %s%s: %d sound tags, %d changed (%d to mono, %d to 22,050 Hz), %d kept, %d not Xbox ADPCM, %d failed; '
          'their samples %.1f MB -> %.1f MB (%.0f%%); the re-encode\'s SNR over %d permutations: median %.1f dB, lowest %.1f' % (
              profile, ' (as s9: the tag has no 11 kHz rate)' if profile == 's10' else
              ' (as s9, the music kept as on the disc)' if profile == 's9full' else
              ' (as s9, the music mono too)' if profile == 's9mono' else '', len(paths),
              stats['changed'], stats['mono'], stats['half rate'], stats['kept'], stats['other format'],
              stats['failed'], before / 1e6, after / 1e6, 100.0 * after / before if before else 100.0,
              len(finite), float(np.median(finite)) if finite else 0.0, min(finite) if finite else 0.0))
    if music:
        size = music.write(pack_path)
        frames = sum(c[2] for c in music.chains)
        print('soundpack music stream: %d permutations in %d chains (%d setups), %.1f min at 44.1 kHz, '
              '%.2f MB (%.0f kbps); %d music tags not at 44.1 kHz left to the cache' % (
                  len(music.entries), len(music.chains), len(music.setups), frames / MUSIC_RATE / 60,
                  size / 1e6, size * 8 / (frames / MUSIC_RATE) / 1000 if frames else 0, music.skipped))
        if adpcm:
            print('soundpack music adpcm: the disc\'s own blocks, unchanged; %d chains left out (not whole blocks)' % music.unaligned)
        if work:
            for leftover in glob.glob(os.path.join(work, '*')):
                os.remove(leftover)
            os.rmdir(work)


if __name__ == '__main__':
    main()
