// SPDX-License-Identifier: MIT
/* Mesa loader entry point for TaruGX using the standard kms_swrast driver. */

struct __DRIextensionRec;

extern const struct __DRIextensionRec **
__driDriverGetExtensions_kms_swrast(void);

__attribute__((visibility("default")))
const struct __DRIextensionRec **
__driDriverGetExtensions_tarugx(void)
{
    return __driDriverGetExtensions_kms_swrast();
}
