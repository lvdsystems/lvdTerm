# Stub. Upstream's ../CMakeLists.txt calls add_subdirectory(docs)
# unconditionally (no BUILD_* guard around it, unlike example/ and
# tests/), and its real docs/CMakeLists.txt (which we don't vendor - see
# VENDORING.md) builds man pages we have no use for. This empty
# replacement just satisfies that add_subdirectory() call.
