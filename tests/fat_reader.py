"""Small independent FAT16 reader for checking packaged images in host tests."""
import struct


class FatVolume:
    def __init__(self, image):
        self.image = image
        self.sector = struct.unpack_from('<H', image, 11)[0]
        self.cluster_size = image[13] * self.sector
        reserved = struct.unpack_from('<H', image, 14)[0]
        fat_size = struct.unpack_from('<H', image, 22)[0] * self.sector
        root_size = struct.unpack_from('<H', image, 17)[0] * 32
        self.fat = image[reserved * self.sector:reserved * self.sector + fat_size]
        root_start = reserved * self.sector + image[16] * fat_size
        self.root = image[root_start:root_start + root_size]
        self.data_start = root_start + ((root_size + self.sector - 1) // self.sector) * self.sector

    def chain(self, cluster):
        data = bytearray()
        seen = set()
        while cluster and cluster < 0xfff8:
            assert 2 <= cluster < 0xfff0 and cluster not in seen, 'invalid FAT chain'
            seen.add(cluster)
            pos = self.data_start + (cluster - 2) * self.cluster_size
            assert pos + self.cluster_size <= len(self.image), 'cluster outside volume'
            data += self.image[pos:pos + self.cluster_size]
            cluster = struct.unpack_from('<H', self.fat, cluster * 2)[0]
            assert cluster, 'allocated chain points to a free cluster'
        return data

    def entries(self, cluster=0):
        data = self.chain(cluster) if cluster else self.root
        entries = {}
        for pos in range(0, len(data), 32):
            record = data[pos:pos + 32]
            if not record[0]:
                break
            if record[0] == 0xe5 or record[11] == 0x0f:
                continue
            name = record[:8].decode('ascii').rstrip()
            ext = record[8:11].decode('ascii').rstrip()
            if ext:
                name += '.' + ext
            entries[name] = (record[11], struct.unpack_from('<H', record, 26)[0],
                             struct.unpack_from('<I', record, 28)[0])
        return entries

    def lookup(self, path):
        cluster = 0
        parts = path.split('/')
        for i, name in enumerate(parts):
            attr, cluster, size = self.entries(cluster)[name]
            if i < len(parts) - 1:
                assert attr & 0x10, 'path component is not a directory'
        return attr, cluster, size

    def read(self, path):
        attr, cluster, size = self.lookup(path)
        assert not attr & 0x10, 'cannot read directory as a file'
        return self.chain(cluster)[:size]
