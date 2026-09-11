# Third-Party Notices

ISDM uses the following external component as a git submodule. The source is
not copied into the ISDM library; it is checked out at the revision recorded
by the superproject's gitlink.

## readerwriterqueue

- Upstream: https://github.com/cameron314/readerwriterqueue
- Submodule path: `thirdparty/readerwriterqueue`
- License: Simplified BSD / BSD 2-Clause
- Copyright: ©2013-2020 Cameron Desrochers
- Upstream license: `thirdparty/readerwriterqueue/LICENSE.md`

ISDM uses `moodycamel::ReaderWriterQueue` through the thin wrapper at
`src/isdm/runtime/spsc_queue.h` for the single-producer/single-consumer path.
