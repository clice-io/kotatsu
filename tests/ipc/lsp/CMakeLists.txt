kota_add_module_tests(LIBS kota::ipc::lsp kota::zest::async)
# lsp_roundtrip instantiates the codec for every LSP type in one translation
# unit, which with clang takes about 2 minutes and 2.7 GB in Debug, 5 minutes
# and 4.2 GB at -O2, and 25 minutes and 8.9 GB under ASan and UBSan at -O2.
kota_add_integration_tests(
    LIBS kota::ipc::lsp
    DEBUG_ONLY roundtrip.test.ts drivers/lsp_roundtrip.cpp
)
