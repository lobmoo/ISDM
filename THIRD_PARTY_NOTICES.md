# Third-Party Notices

This project vendors the following third-party component in the source tree.
The corresponding license text is kept next to the vendored source and must be
included in source and binary distributions.

## readerwriterqueue

- Upstream: https://github.com/cameron314/readerwriterqueue
- Copyright: © 2013-2020 Cameron Desrochers
- License: BSD 2-Clause (Simplified BSD)
- Vendored headers: `thirdparty/readerwriterqueue/inlcude/`
- License text: `thirdparty/readerwriterqueue/LICENSE`

The project uses `moodycamel::ReaderWriterQueue` through
`src/isdm/runtime/spsc_queue.h` for the single-producer/single-consumer path.
