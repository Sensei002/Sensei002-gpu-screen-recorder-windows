/* platform/windows/stubs/drm_mode.h — stub <drm_mode.h> for the Windows build.
 *
 * Upstream's kms/kms_shared.h includes the libdrm header <drm_mode.h>,
 * which does not exist on Windows. The only symbol the headers compiled on
 * Windows use from it is `struct hdr_output_metadata` (a member of
 * gsr_kms_response_item). The Windows port never instantiates or reads that
 * struct, so any definition keeps the declaration valid.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3c.
 */
#ifndef GSR_STUB_DRM_MODE_H
#define GSR_STUB_DRM_MODE_H

#include <stdint.h>

struct hdr_output_metadata {
    uint32_t metadata_type;
    uint32_t reserved[4];
};

#endif /* GSR_STUB_DRM_MODE_H */
