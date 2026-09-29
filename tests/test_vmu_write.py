"""VMU file service: allocation, overwrite, delete, limits, corruption refusal and
fault injection, against a writable in-memory card model.

The C engine (src/dreamcast/vmu_file.c) runs on the host under ASan/UBSan
through tests/vmu_write_host.c, twice when possible:
  * with tests/vmufs_double.c, a step-for-step port of KOS's vmufs behaviour;
  * with KOS's own unmodified vmufs.c, if an SDK is installed (KOS_BASE).
Results are compared with the independent Python model in tools/vmu_fixture.py.
Real Dreamcast hardware and real VMU flash timing are NOT exercised."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vmu_fixture', ROOT / 'tools' / 'vmu_fixture.py')
fx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fx)
VmuCard = fx.VmuCard

IO, CARD, NOT_FOUND, EXISTS, FULL, DIR_FULL, PROTECTED, BAD_NAME = -2, -3, -4, -5, -6, -7, -8, -9
TOO_BIG, VERIFY, RESTORED, AMBIGUOUS, BUFFER = -10, -11, -12, -13, -16


def kos_vmufs_source():
    base = Path(os.environ.get('KOS_BASE', Path.home() / '.local/share/dreamcast/kos'))
    source = base / 'kernel/arch/dreamcast/fs/vmufs.c'
    include = base / 'kernel/arch/dreamcast/include'
    return (source, include) if source.is_file() and (include / 'dc/vmufs.h').is_file() else None


def compile_driver(out, real):
    cc = os.environ.get('CC', 'cc')
    cmd = [cc, '-std=gnu11', '-Wall', '-Wextra', '-g', '-fsanitize=address,undefined',
           '-Iinclude', '-Itests/vmufs_shim', 'tests/vmu_write_host.c',
           'src/dreamcast/vmu_file.c', 'src/dreamcast/vmu_info.c']
    if real:
        source, include = real
        cmd += ['-I' + str(include), str(source), '-Wno-format']
    else:
        cmd += ['tests/vmufs_double.c']
    subprocess.run(cmd + ['-o', str(out)], cwd=ROOT, check=True)


class Host:
    """One compiled driver plus a scratch image."""
    def __init__(self, exe, tmp):
        self.exe, self.tmp, self.image = exe, Path(tmp), Path(tmp) / 'card.bin'

    def run(self, card, *args, env=None):
        """Run one command on a copy of card; returns (result, reads, writes, new_card_bytes)."""
        self.image.write_bytes(bytes(card.data if isinstance(card, VmuCard) else card))
        full_env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        full_env.update({k: str(v) for k, v in (env or {}).items()})
        done = subprocess.run([str(self.exe), str(self.image), *map(str, args)], cwd=ROOT,
                              env=full_env, capture_output=True, text=True)
        self.last = done
        assert done.returncode == 0, done.stdout + done.stderr
        fields = dict(part.split('=') for part in done.stdout.split())
        return int(fields['result']), int(fields['reads']), int(fields['writes']), self.image.read_bytes()

    def write(self, card, name, payload, overwrite=False, env=None):
        source = self.tmp / 'input.bin'
        source.write_bytes(payload)
        args = ['write', name, str(source)] + (['overwrite'] if overwrite else [])
        return self.run(card, *args, env=env)

    def read(self, card, name, env=None):
        out = self.tmp / 'output.bin'
        if out.exists():
            out.unlink()
        result = self.run(card, 'read', name, str(out), env=env)
        return result, (out.read_bytes() if out.exists() else None)


class Variants(unittest.TestCase):
    """Subclasses run every test once per driver variant."""
    hosts = {}

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.hosts = {}
        variants = {'double': None}
        real = kos_vmufs_source()
        if real:
            variants['kos'] = real
        for name, source in variants.items():
            exe = Path(cls.tmp.name) / f'vmuw_{name}'
            compile_driver(exe, source)
            cls.hosts[name] = Host(exe, cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def each(self):
        for name, host in self.hosts.items():
            with self.subTest(vmufs=name):
                yield host


def payload(size, seed=1):
    return bytes((i * 7 + seed) & 255 for i in range(size))


def blank_card(dir_size=13):
    data = bytearray(fx.image(0))
    struct.pack_into('<6H', data, 255 * 512 + 70, 254, 1, 253, dir_size, 0, 200)
    return VmuCard(data)


class WriteDeleteTests(Variants):
    def test_create_overwrite_delete_match_kos_model(self):
        for host in self.each():
            card = VmuCard(fx.image(3))
            model = VmuCard(fx.image(3))
            steps = [('create', 1000, False, 1024), ('again', 1000, False, EXISTS),
                     ('grow', 3000, True, 3072), ('shrink', 100, True, 512),
                     ('exact', 1024, True, 1024)]
            for label, size, overwrite, expect in steps:
                before = bytes(card.data)
                data = payload(size, size)
                result, _, _, after = host.write(card, 'NEWFILE.DAT', data, overwrite)
                self.assertEqual(result, expect, label)
                if expect > 0:
                    self.assertEqual(model.kos_write(b'NEWFILE.DAT', data, overwrite), 0)
                    self.assertEqual(VmuCard(after).masked(), model.masked(), label)
                    self.assertEqual(VmuCard(after).fsck(), [], label)
                    self.assertEqual(VmuCard(after).read(b'NEWFILE.DAT')[:size], data)
                else:
                    self.assertEqual(after, before, label)  # a refusal never writes
                card = VmuCard(after)
            result, got = host.read(card, 'NEWFILE.DAT')
            self.assertEqual(result[0], 1024)
            self.assertEqual(got[:1024], data)
            result, _, _, after = host.run(card, 'delete', 'NEWFILE.DAT')
            self.assertEqual(result, 0)
            model.kos_delete(b'NEWFILE.DAT')
            self.assertEqual(VmuCard(after).masked(), model.masked())
            self.assertEqual(VmuCard(after).fsck(), [])
            self.assertEqual(VmuCard(after).free_blocks(), 194)
            result, _, writes, again = host.run(after, 'delete', 'NEWFILE.DAT')
            self.assertEqual((result, writes), (NOT_FOUND, 0))
            self.assertEqual(again, after)

    def test_multiple_files_allocate_like_kos(self):
        for host in self.each():
            card = VmuCard(fx.image(0))
            model = VmuCard(fx.image(0))
            for i, size in enumerate([512, 513, 5000, 1, 40000]):
                name = f'F{i}.BIN'
                data = payload(size, i)
                result, _, _, after = host.write(card, name, data)
                self.assertEqual(result, (size + 511) // 512 * 512)
                model.kos_write(name.encode(), data)
                card = VmuCard(after)
                self.assertEqual(card.masked(), model.masked())
                self.assertEqual(card.fsck(), [])
            # Deleting the middle file frees exactly its blocks; nothing else moves.
            result, _, _, after = host.run(card, 'delete', 'F2.BIN')
            model.kos_delete(b'F2.BIN')
            self.assertEqual(VmuCard(after).masked(), model.masked())
            self.assertEqual(VmuCard(after).fsck(), [])

    def test_two_hundred_block_limits(self):
        for host in self.each():
            card = blank_card()
            big = payload(200 * 512)
            result, _, _, after = host.write(card, 'FULL.BIN', big)
            self.assertEqual(result, 200 * 512)
            card = VmuCard(after)
            self.assertEqual((card.free_blocks(), card.fsck()), (0, []))
            self.assertEqual(card.read(b'FULL.BIN'), big)
            result, _, writes, same = host.write(card, 'MORE.BIN', b'x')
            self.assertEqual((result, writes, same), (FULL, 0, after))
            result, _, _, after2 = host.write(card, 'FULL.BIN', big[::-1], overwrite=True)
            self.assertEqual(result, 200 * 512)  # an overwrite reuses its own blocks
            self.assertEqual(VmuCard(after2).read(b'FULL.BIN'), big[::-1])
            self.assertEqual(VmuCard(after2).fsck(), [])
            result, _, writes, unchanged = host.write(blank_card(), 'HUGE.BIN', payload(200 * 512 + 1))
            self.assertEqual((result, writes), (TOO_BIG, 0))
            result, _, writes, unchanged = host.write(blank_card(), 'NONE.BIN', b'')
            self.assertEqual((result, writes), (-64, 0))
            # 150 used by others: 100-block file may be replaced by 150, not 151.
            card = blank_card()
            card.kos_write(b'A', payload(100 * 512))
            card.kos_write(b'B', payload(50 * 512))
            result, _, writes, after = host.write(card, 'A', payload(151 * 512), overwrite=True)
            self.assertEqual((result, writes), (FULL, 0))
            result, _, _, after = host.write(card, 'A', payload(150 * 512), overwrite=True)
            self.assertEqual(result, 150 * 512)
            self.assertEqual(VmuCard(after).fsck(), [])
            self.assertEqual(VmuCard(after).read(b'B'), payload(50 * 512))

    def test_directory_full_is_refused_before_any_write(self):
        for host in self.each():
            card = blank_card(dir_size=1)  # 16 directory entries
            for i in range(16):
                card.kos_write(f'E{i:02d}'.encode(), payload(10, i))
            self.assertEqual(len(card.entries()), 16)
            result, _, writes, after = host.write(card, 'ONEMORE', b'x')
            self.assertEqual((result, writes, after), (DIR_FULL, 0, bytes(card.data)))
            result, _, _, after = host.write(card, 'E07', payload(700), overwrite=True)
            self.assertEqual(result, 1024)
            self.assertEqual(VmuCard(after).fsck(), [])

    def test_names(self):
        for host in self.each():
            card = VmuCard(fx.image(0))
            for bad in ['', 'A' * 13, ' LEADING', '\tTAB', 'ok\x7f', '   ']:
                if '\x00' in bad:
                    continue
                result, _, writes, after = host.write(card, bad, b'x')
                self.assertEqual((result, writes), (BAD_NAME, 0), repr(bad))
            result, _, _, after = host.write(card, 'TWELVE_CHARS', b'x')
            self.assertEqual(result, 512)
            result, _, _, after = host.write(after, 'trim  ', b'y')  # trailing spaces trimmed
            entries = VmuCard(after).entries()
            self.assertEqual(sorted(e['name'] for e in entries),
                             sorted([b'TWELVE_CHARS', b'trim'.ljust(12, b'\0')]))

    def test_space_padded_and_dirty_names_use_the_exact_stored_bytes(self):
        for host in self.each():
            card = VmuCard(fx.image(0))
            card.kos_write(b'OLD.TXT', payload(600))
            e = card.entries()[0]
            o = card.entry_offset(e['index'])
            card.data[o + 4:o + 16] = b'OLD.TXT     '  # space padded, as some tools write it
            model = VmuCard(card.data)
            result, _, _, after = host.write(card, 'OLD.TXT', payload(2000, 9), overwrite=True)
            self.assertEqual(result, 2048)
            model.kos_write(b'OLD.TXT     ', payload(2000, 9), overwrite=True)
            self.assertEqual(VmuCard(after).masked(), model.masked())
            self.assertEqual(len(VmuCard(after).entries()), 1)
            self.assertEqual(VmuCard(after).entries()[0]['name'], b'OLD.TXT     ')
            # Without OVERWRITE it must not create a look-alike duplicate.
            result, _, writes, same = host.write(card, 'OLD.TXT', b'z')
            self.assertEqual((result, writes), (EXISTS, 0))
            result, _, _, after = host.run(card, 'delete', 'OLD.TXT')
            self.assertEqual(result, 0)
            self.assertEqual(VmuCard(after).entries(), [])
            # A name with junk after its NUL still resolves to a single entry.
            junk = VmuCard(fx.image(0))
            junk.kos_write(b'ABC', payload(10))
            o = junk.entry_offset(junk.entries()[0]['index'])
            junk.data[o + 4:o + 16] = b'ABC\0XYZ12345'
            result, _, _, after = host.write(junk, 'ABC', payload(20), overwrite=True)
            self.assertEqual(result, 512)
            self.assertEqual(VmuCard(after).fsck(), [])

    def test_ambiguous_names_are_refused(self):
        for host in self.each():
            card = VmuCard(fx.image(0))
            card.kos_write(b'DUP', payload(10))
            card.kos_write(b'DUQ', payload(10))
            e = card.entries()[1]
            o = card.entry_offset(e['index'])
            card.data[o + 4:o + 16] = b'DUP         '  # same identity as DUP, different bytes
            for command in (['write', 'DUP', None, 'overwrite'], ['delete', 'DUP']):
                if None in command:
                    src = host.tmp / 'x.bin'
                    src.write_bytes(b'q')
                    command[2] = str(src)
                result, _, writes, after = host.run(card, *command)
                self.assertEqual((result, writes, after), (AMBIGUOUS, 0, bytes(card.data)), command)

    def test_protected_games_and_odd_files_are_never_changed(self):
        for host in self.each():
            card = VmuCard(fx.image(0))
            for name in (b'LOCKED', b'GAME', b'ODDHDR', b'PLAIN'):
                card.kos_write(name, payload(1000))
            by_name = {e['name'].rstrip(b'\0'): card.entry_offset(e['index']) for e in card.entries()}
            card.data[by_name[b'LOCKED'] + 1] = 0xff
            card.data[by_name[b'GAME']] = 0xcc
            struct.pack_into('<H', card.data, by_name[b'ODDHDR'] + 26, 1)
            for name in ('LOCKED', 'GAME', 'ODDHDR'):
                result, _, writes, after = host.write(card, name, b'x', overwrite=True)
                self.assertEqual((result, writes, after), (PROTECTED, 0, bytes(card.data)), name)
                result, _, writes, after = host.run(card, 'delete', name)
                self.assertEqual((result, writes, after), (PROTECTED, 0, bytes(card.data)), name)
            self.assertEqual(host.read(card, 'LOCKED')[0][0], PROTECTED)  # honour copy protection
            self.assertEqual(host.read(card, 'GAME')[0][0], 1024)           # a game is readable
            result, _, _, after = host.run(card, 'delete', 'PLAIN')
            self.assertEqual(result, 0)

    def test_read_size_query_and_small_buffer(self):
        for host in self.each():
            card = VmuCard(fx.image(0))
            card.kos_write(b'DATA', payload(1500))
            self.assertEqual(host.run(card, 'size', 'DATA')[0], 1536)
            self.assertEqual(host.run(card, 'size', 'NOPE')[0], NOT_FOUND)
            out = host.tmp / 'small.bin'
            self.assertEqual(host.run(card, 'readn', 'DATA', 1535, str(out))[0], BUFFER)
            self.assertEqual(host.run(card, 'readn', 'DATA', 1536, str(out))[0], 1536)
            self.assertEqual(out.read_bytes()[:1500], payload(1500))


def damage_cases():
    """(label, mutator) pairs producing a card the engine must refuse to touch."""
    def cross_link(c):
        struct.pack_into('<H', c.data, c.entry_offset(1) + 2, c.entry(0)['first'])
    def cycle(c):
        e = c.entry(2)
        c.fat_set(e['first'], e['first'])
    def short_chain(c):
        e = c.entry(2)
        struct.pack_into('<H', c.data, c.entry_offset(2) + 24, e['blocks'] + 1)
    def long_chain(c):
        e = c.entry(2)
        c.fat_set(c.chain(e)[0], fx.FAT_END)
    def free_in_chain(c):
        e = c.entry(2)
        c.fat_set(c.chain(e)[1], 0xfffc)
    def first_out_of_range(c):
        struct.pack_into('<H', c.data, c.entry_offset(0) + 2, 210)
    def zero_blocks(c):
        struct.pack_into('<H', c.data, c.entry_offset(0) + 24, 0)
    def bad_type(c):
        c.data[c.entry_offset(0)] = 0x77
    def bad_magic(c):
        c.data[255 * 512] = 0
    def two_fat_blocks(c):
        struct.pack_into('<H', c.data, 255 * 512 + 72, 2)
    def oversize_card(c):
        struct.pack_into('<H', c.data, 255 * 512 + 80, 250)
    def directory_overlaps_fat(c):
        struct.pack_into('<H', c.data, 255 * 512 + 74, 254)
    return [('cross-link', cross_link), ('cycle', cycle), ('short chain', short_chain),
            ('chain longer than entry', long_chain), ('free block inside chain', free_in_chain),
            ('first block out of range', first_out_of_range), ('zero block count', zero_blocks),
            ('bad file type', bad_type), ('unformatted', bad_magic), ('two FAT blocks', two_fat_blocks),
            ('expanded card', oversize_card), ('directory over FAT', directory_overlaps_fat)]


class CorruptionTests(Variants):
    def test_damaged_cards_are_refused_without_a_single_write(self):
        for host in self.each():
            for label, mutate in damage_cases():
                card = VmuCard(fx.image(3))
                mutate(card)
                snapshot = bytes(card.data)
                src = host.tmp / 'x.bin'
                src.write_bytes(b'data')
                for command in (['write', 'BRANDNEW', str(src)], ['write', 'NOTE01.TXT', str(src), 'overwrite'],
                                ['delete', 'NOTE01.TXT'], ['delete', 'NOTE03.TXT'], ['size', 'NOTE01.TXT']):
                    result, _, writes, after = host.run(card, *command)
                    self.assertEqual((result, writes), (CARD, 0), f'{label}: {command}')
                    self.assertEqual(after, snapshot, label)

    def test_unreadable_metadata_blocks_stop_before_writing(self):
        for host in self.each():
            card = VmuCard(fx.image(3))
            src = host.tmp / 'x.bin'
            src.write_bytes(b'data')
            for block in [255, 254] + list(range(253, 240, -1)):
                result, _, writes, after = host.run(card, 'write', 'BRANDNEW', str(src),
                                                    env={'VMU_FAIL_READ': block})
                self.assertEqual((result, writes), (IO, 0), block)
                self.assertEqual(after, bytes(card.data))
            # An unreadable block of the file being replaced blocks the overwrite (no rollback copy).
            first = card.entry(1)['first']
            result, _, writes, after = host.run(card, 'write', 'NOTE02.TXT', str(src), 'overwrite',
                                                env={'VMU_FAIL_READ': first})
            self.assertEqual((result, writes, after), (IO, 0, bytes(card.data)))


class FaultInjectionTests(Variants):
    """A card fault mid-write must leave a consistent card and an honest result."""

    def scenario_cards(self):
        base = VmuCard(fx.image(3))
        base.kos_write(b'TARGET', payload(1200, 5))
        return base

    def check_outcome(self, host, before_card, result, after, kind, expected_card):
        after_card = VmuCard(after)
        if result >= 0:
            self.assertEqual(after_card.masked(), expected_card.masked(), kind)
            self.assertEqual(after_card.fsck(), [], kind)
        elif result in (IO, RESTORED):
            # The promise: nothing visible changed.
            self.assertEqual(after_card.fsck(), [], f'{kind}: {result}')
            self.assertEqual(sorted((e['name'], after_card.read(e['name'])) for e in after_card.entries()),
                             sorted((e['name'], before_card.read(e['name'])) for e in before_card.entries()), kind)
            self.assertEqual(after_card.free_blocks(), before_card.free_blocks(), kind)
        else:
            self.assertEqual(result, VERIFY, f'{kind}: unexpected result {result}')

    def run_matrix(self, host, kind, command, expected_card, env_name, limit=14):
        card = self.scenario_cards()
        seen = set()
        for k in range(1, limit):
            src = host.tmp / 'in.bin'
            result, _, writes, after = host.run(card, *[str(src) if a == 'SRC' else a for a in command],
                                                env={env_name: k})
            seen.add(result)
            self.check_outcome(host, card, result, after, f'{kind} {env_name}={k}', expected_card)
        return seen

    def test_transient_and_permanent_faults(self):
        for host in self.each():
            src = host.tmp / 'in.bin'
            src.write_bytes(payload(3000, 3))
            create = self.scenario_cards()
            create.kos_write(b'NEWONE', payload(3000, 3))
            overwrite = self.scenario_cards()
            overwrite.kos_write(b'TARGET', payload(3000, 3), overwrite=True)
            shrink = self.scenario_cards()
            shrink.kos_write(b'TARGET', payload(3000, 3), overwrite=True)
            delete = self.scenario_cards()
            delete.kos_delete(b'TARGET')
            table = [('create', ['write', 'NEWONE', 'SRC'], create),
                     ('overwrite', ['write', 'TARGET', 'SRC', 'overwrite'], overwrite),
                     ('delete', ['delete', 'TARGET'], delete)]
            outcomes = {}
            for kind, command, expected in table:
                for env in ('VMU_FAIL_WRITE_N', 'VMU_FAIL_WRITE_FROM'):
                    outcomes[(kind, env)] = self.run_matrix(host, kind, command, expected, env)
            # The interesting paths must really have been exercised.
            self.assertIn(RESTORED, outcomes[('overwrite', 'VMU_FAIL_WRITE_N')], outcomes)
            self.assertIn(IO, outcomes[('create', 'VMU_FAIL_WRITE_N')], outcomes)
            self.assertTrue(outcomes[('create', 'VMU_FAIL_WRITE_FROM')] & {IO, VERIFY})

    def test_silent_corruption_is_caught_by_readback(self):
        for host in self.each():
            src = host.tmp / 'in.bin'
            src.write_bytes(payload(3000, 3))
            card = self.scenario_cards()
            created = self.scenario_cards()
            created.kos_write(b'NEWONE', payload(3000, 3))
            caught = 0
            for k in range(1, 8):
                result, _, writes, after = host.run(card, 'write', 'NEWONE', str(src), env={'VMU_FLIP_WRITE_N': k})
                if result >= 0:
                    # Harmless flips must still yield exactly the intended card.
                    self.assertEqual(VmuCard(after).masked(), created.masked())
                else:
                    caught += 1
                    self.assertIn(result, (VERIFY, IO, RESTORED))
            self.assertGreaterEqual(caught, 3)
            over = self.scenario_cards()
            expected = self.scenario_cards()
            expected.kos_write(b'TARGET', payload(3000, 3), overwrite=True)
            restored = 0
            for k in range(1, 8):
                result, _, _, after = host.run(over, 'write', 'TARGET', str(src), 'overwrite',
                                               env={'VMU_FLIP_WRITE_N': k})
                if result >= 0:
                    self.assertEqual(VmuCard(after).masked(), expected.masked())
                elif result == RESTORED:
                    restored += 1
                    self.assertEqual(VmuCard(after).fsck(), [])
                    self.assertEqual(VmuCard(after).read(b'TARGET'), over.read(b'TARGET'))
            self.assertGreaterEqual(restored, 1)


class FixtureTests(Variants):
    def test_editor_fixture_card_is_valid_and_readable_by_the_engine(self):
        card = fx.editor_card()
        self.assertEqual(card.fsck(), [])
        names = [e['name'].rstrip(b'\0') for e in card.entries()]
        self.assertEqual(names, [b'ICONTEST', b'ANIMATED.001', b'RAWDATA.BIN', b'LOCKED', b'MINIGAME'])
        for host in self.each():
            result, got = host.read(card, 'ICONTEST')
            self.assertEqual(result[0], len(card.read(b'ICONTEST')))
            self.assertEqual(got, card.read(b'ICONTEST'))
            self.assertEqual(host.read(card, 'LOCKED')[0][0], PROTECTED)


class ModelTests(unittest.TestCase):
    def test_model_detects_the_corruptions_the_engine_refuses(self):
        for label, mutate in damage_cases():
            card = VmuCard(fx.image(3))
            self.assertEqual(card.fsck(), [])
            mutate(card)
            if label in ('two FAT blocks', 'expanded card', 'directory over FAT'):
                continue  # layout errors the model does not lint; the engine refuses them
            self.assertNotEqual(card.fsck(), [], label)

    def test_model_follows_kos_allocation_order(self):
        card = VmuCard(fx.image(0))
        card.kos_write(b'A', payload(1024))
        self.assertEqual(card.chain(card.entries()[0]), [199, 198])  # data files fill from the top
        card.kos_write(b'B', b'x')
        self.assertEqual(card.entries()[1]['first'], 197)
        card.kos_delete(b'A')
        self.assertEqual(card.free_blocks(), 199)
        self.assertEqual(card.fsck(), [])


if __name__ == '__main__':
    unittest.main()
