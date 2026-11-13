"""Run with: python -m unittest discover -s tests -p test_build_pages.py"""
from contextlib import redirect_stdout
from datetime import datetime, timezone
from html.parser import HTMLParser
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_pages


def release(tag, published, prerelease=False, **extra):
    return dict(tag_name=tag, name=tag, published_at=published,
                prerelease=prerelease, html_url=f'https://example.com/{tag}', assets=[], **extra)


class Disclosures(HTMLParser):
    def __init__(self, document):
        super().__init__()
        self.toggles = {}
        self.labels = set()
        self.feed(document)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'input' and attrs.get('type') == 'checkbox':
            self.toggles[attrs['id']] = attrs
        if tag == 'label':
            self.labels.add(attrs.get('for'))


class PortalTest(unittest.TestCase):
    def build(self, releases):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        source = root / 'releases.json'
        source.write_text(json.dumps(releases), encoding='utf-8')
        mini = root / 'spi.bin'
        mini.write_bytes(b'test firmware')
        output = root / 'site'
        with patch.object(sys, 'argv', ['build_pages.py', '--releases-json', str(source),
                                       '--spi-mini-bin', str(mini), '-o', str(output)]), \
                patch.object(build_pages, 'urlopen', return_value=io.BytesIO(b'<html><head></head></html>')) as download, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(build_pages.main(), 0)
        self.output, self.download = output, download
        return (output / 'index.html').read_text(encoding='utf-8')

    def rows(self, document, group):
        match = re.search(fr'<ul\b[^>]*\bid="{group}">(.*?)</ul>', document, re.DOTALL)
        if not match:
            return []
        content = match.group(1)
        return re.findall(r'<div class="version"><a href="[^"]*">([^<]*)</a>', content)

    def test_publication_boundary_sorting_and_disclosure(self):
        # Deliberately unordered; published_at takes precedence over build tags
        # and created_at (a stable release can promote an older commit).
        document = self.build([
            release('202611131000-dev', '2026-11-13T10:00:00Z', True),
            release('202611140000-dev', '2026-11-11T10:00:00Z', True),
            release('202611120000', '2026-11-13T09:00:00Z'),
            release('202611131100-dev', '2026-11-13T11:00:00Z', True),
            release('202611110000', '2026-11-11T09:00:00Z'),
            release('equal-time-dev', '2026-11-13T09:00:00Z', True),
            release('draft-stable', '2026-11-14T00:00:00Z', draft=True),
            release('../invalid', '2026-11-15T00:00:00Z'),
        ])
        self.assertEqual(self.rows(document, 'stable-releases'), ['202611120000', '202611110000'])
        self.assertEqual(self.rows(document, 'recent-prereleases'), ['202611131100-dev', '202611131000-dev'])
        self.assertEqual(self.rows(document, 'older-prereleases'), ['equal-time-dev', '202611140000-dev'])
        disclosures = Disclosures(document)
        self.assertEqual(set(disclosures.toggles), {'prereleases-toggle'})
        self.assertEqual(disclosures.labels, set(disclosures.toggles))
        for toggle in disclosures.toggles.values():
            self.assertNotIn('checked', toggle, 'pre-release histories start collapsed')
            self.assertEqual(toggle['aria-controls'], 'recent-prereleases older-prereleases')
        self.assertEqual(document.count('>202611131100-dev</a>'), 1, 'latest is not duplicated in collapsed groups')
        self.assertIn('>PRE-RELEASES</h2>', document)
        self.assertIn('>OLDER PRE-RELEASES</h2>', document)
        self.assertNotIn('<details', document)
        self.assertIn('Show more', document)
        self.assertIn('Show less', document)
        self.assertNotIn('draft-stable', document)
        self.assertNotIn('../invalid', document)
        self.download.assert_not_called()

    def test_all_prereleases_are_old(self):
        document = self.build([
            release('stable', '2026-11-13T10:00:00Z'),
            release('previous-dev', '2026-11-12T10:00:00Z', True),
        ])
        self.assertNotIn('id="recent-prereleases-section"', document, 'hide the entire newer block when superseded by stable')
        self.assertEqual(self.rows(document, 'older-prereleases'), ['previous-dev'])
        self.assertEqual(Disclosures(document).toggles['prereleases-toggle']['aria-controls'], 'older-prereleases')

    def test_no_stable_release(self):
        document = self.build([release('dev-only', '2026-11-13T10:00:00Z', True),
                               release('earlier-dev', '2026-11-13T09:00:00Z', True)])
        self.assertIn('>PRE-RELEASES</h2>', document)
        self.assertEqual(self.rows(document, 'recent-prereleases'), ['dev-only', 'earlier-dev'])
        self.assertNotIn('checked', Disclosures(document).toggles['prereleases-toggle'])
        self.assertEqual(self.rows(document, 'older-prereleases'), [])

    def test_assets_and_snapshots_still_work(self):
        entry = release('202611112101', '2026-11-11T21:03:28Z', True)
        entry['assets'] = [dict(name=name, url=f'https://example.com/api/{name}',
                                browser_download_url=f'https://example.com/download/{name}')
                           for name in ('Dosimeter.html', 'Dosimeter.exe', 'firmware.bin', 'spi-service-firmware.bin')]
        document = self.build([entry])
        self.assertIn('href="./versions/202611112101/"', document)
        self.assertIn('<span class="release-date">2026-11-11 21:01 UTC · 202611112101</span>', document)
        links = re.search(r'<span class="downloads">(.*?)</span>', document).group(1)
        self.assertEqual(re.findall(r'>([^<]+)</a>', links), ['Open', 'HTML', 'Windows', 'BIN', 'SPI service', 'notes'])
        for name in ('Dosimeter.html', 'Dosimeter.exe', 'firmware.bin', 'spi-service-firmware.bin'):
            self.assertIn(f'https://example.com/download/{name}', document)
        self.assertTrue((self.output / 'versions/202611112101/manifest.webmanifest').is_file())
        self.assertIn('manifest.webmanifest', (self.output / 'versions/202611112101/index.html').read_text())
        self.download.assert_called_once()

    def test_only_one_new_prerelease(self):
        document = self.build([
            release('latest-dev', '2026-11-13T10:00:00Z', True),
            release('stable', '2026-11-12T10:00:00Z'),
        ])
        self.assertEqual(self.rows(document, 'recent-prereleases'), ['latest-dev'])
        self.assertIn('>PRE-RELEASES</h2>', document)
        self.assertEqual(Disclosures(document).toggles, {}, 'single-row groups need no Show more')
        self.assertNotIn('class="show-more"', document)

    def test_one_new_prerelease_and_archive_share_one_toggle(self):
        document = self.build([
            release('latest-dev', '2026-11-13T10:00:00Z', True),
            release('stable', '2026-11-12T10:00:00Z'),
            release('older-dev', '2026-11-11T10:00:00Z', True),
        ])
        disclosures = Disclosures(document)
        self.assertEqual(set(disclosures.toggles), {'prereleases-toggle'})
        self.assertEqual(document.count('class="show-more"'), 1)
        self.assertNotIn('checked', disclosures.toggles['prereleases-toggle'])
        self.assertEqual(disclosures.toggles['prereleases-toggle']['aria-controls'],
                         'recent-prereleases older-prereleases')
        self.assertLess(document.index('id="prereleases-toggle"'), document.index('id="recent-prereleases-section"'))
        self.assertLess(document.index('id="older-prereleases-section"'), document.index('for="prereleases-toggle"'))

    def test_legacy_dates_and_timezones(self):
        self.assertEqual(build_pages.release_time(dict(tag_name='202611131015-dev')),
                         datetime(2026, 11, 13, 10, 15, tzinfo=timezone.utc))
        self.assertEqual(build_pages.release_time(dict(tag_name='202611131015-dev', published_at='invalid')),
                         datetime(2026, 11, 13, 10, 15, tzinfo=timezone.utc))
        self.assertLess(build_pages.release_time(dict(tag_name='202699999999-dev')),
                        build_pages.release_time(dict(published_at='2026-11-13T12:00:00+02:00')))

    def test_stable_and_prerelease_histories_have_independent_toggles(self):
        document = self.build([
            *[release(f'stable-{day}', f'2026-11-0{day}T10:00:00Z') for day in range(1, 6)],
            release('latest-dev', '2026-11-07T10:00:00Z', True),
            release('next-dev', '2026-11-06T10:00:00Z', True),
            release('older-dev', '2026-11-04T10:00:00Z', True),
        ])
        disclosures = Disclosures(document)
        self.assertEqual(set(disclosures.toggles), {'releases-toggle', 'prereleases-toggle'})
        self.assertEqual(disclosures.labels, set(disclosures.toggles))
        self.assertEqual(disclosures.toggles['releases-toggle']['aria-controls'], 'stable-releases')
        self.assertEqual(disclosures.toggles['prereleases-toggle']['aria-controls'],
                         'recent-prereleases older-prereleases')
        self.assertTrue(all('checked' not in attrs for attrs in disclosures.toggles.values()))
        self.assertEqual(len(self.rows(document, 'stable-releases')), 5, 'older stable rows remain available to expand')
        self.assertEqual(document.count('class="show-more"'), 2)

    def test_stable_only_expansion(self):
        document = self.build([release(f'stable-{day}', f'2026-11-0{day}T10:00:00Z') for day in range(1, 5)])
        self.assertEqual(set(Disclosures(document).toggles), {'releases-toggle'})
        self.assertNotIn('id="prereleases-toggle"', document)

    def test_no_prereleases(self):
        document = self.build([release('stable', '2026-11-13T10:00:00Z')])
        self.assertEqual(Disclosures(document).toggles, {})
        self.assertNotIn('id="recent-prereleases-section"', document)
        self.assertNotIn('id="older-prereleases-section"', document)


if __name__ == '__main__':
    unittest.main()
