#if defined(_WIN32)
#include "native_preset_storage_windows_rename_shim.h"
#endif

#include "native_preset_storage_impl.inc"

#if defined(_WIN32)
#undef SetFileInformationByHandle
#endif
