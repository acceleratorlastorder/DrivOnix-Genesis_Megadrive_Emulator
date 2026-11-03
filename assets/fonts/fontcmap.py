"""Read Unicode cmap coverage from pinned SFNT fonts using only the stdlib."""
import struct


def coverage(data):
    def u16(at):
        return struct.unpack_from(">H", data, at)[0]

    def u32(at):
        return struct.unpack_from(">I", data, at)[0]

    tables = {}
    for index in range(u16(4)):
        at = 12 + index * 16
        tag, _, offset, length = struct.unpack_from(">4sIII", data, at)
        if offset + length > len(data):
            raise ValueError("Truncated SFNT table")
        tables[tag] = (offset, length)
    start, length = tables[b"cmap"]
    end = start + length
    points = set()
    for index in range(u16(start + 2)):
        at = start + 4 + index * 8
        platform, encoding, offset = struct.unpack_from(">HHI", data, at)
        if platform != 0 and (platform != 3 or encoding not in (1, 10)):
            continue
        base = start + offset
        kind = u16(base)
        if kind == 4:
            limit = base + u16(base + 2)
            if limit > end:
                raise ValueError("Truncated format 4 cmap")
            count = u16(base + 6) // 2
            ends = base + 14
            starts = ends + count * 2 + 2
            deltas = starts + count * 2
            offsets = deltas + count * 2
            for segment in range(count):
                first, last = u16(starts + segment * 2), u16(ends + segment * 2)
                delta, relative = u16(deltas + segment * 2), u16(offsets + segment * 2)
                for point in range(first, min(last, 0xfffe) + 1):
                    if relative:
                        address = offsets + segment * 2 + relative + (point - first) * 2
                        if address + 2 > limit:
                            raise ValueError("Cmap glyph offset outside table")
                        glyph = u16(address)
                        if glyph:
                            glyph = (glyph + delta) & 0xffff
                    else:
                        glyph = (point + delta) & 0xffff
                    if glyph:
                        points.add(point)
        elif kind == 12:
            limit = base + u32(base + 4)
            if limit > end:
                raise ValueError("Truncated format 12 cmap")
            for group in range(u32(base + 12)):
                first, last, glyph = struct.unpack_from(">III", data, base + 16 + group * 12)
                if first > last or last > 0x10ffff:
                    raise ValueError("Invalid Unicode cmap range")
                points.update(range(first + (glyph == 0), last + 1))
    return {point for point in points if not 0xd800 <= point <= 0xdfff}


def ranges(points):
    result = []
    for point in sorted(points):
        if result and point == result[-1][1] + 1:
            result[-1][1] = point
        else:
            result.append([point, point])
    return result
