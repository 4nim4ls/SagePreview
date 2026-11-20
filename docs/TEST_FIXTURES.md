# Test fixture provenance

Synthetic test fixtures are developer-oriented assets, not example artwork from real artists.

- `tests/generate_fixture.py` generates the SQLite-backed synthetic CLIP file samples using `samples/preview-test.png`.
- `tests/generate_kra_fixtures.py` generates ZIP-style Krita sample documents with stored/DEFLATE variants and deliberate corruptions.
- `tests/generate_psd_fixtures.py` generates compact PSD samples for tested compression modes and invalid variants.
- `samples/synthetic-avif.avif` and `samples/webp-codec-test.webp` are small codec-check assets created during development.

The images are retained only as test inputs. If you redistribute the source or adjust test assets, retain their purpose and regenerate any dependent files consistently. The project copyright owner should confirm that all binary test assets can be redistributed before publishing.
