#ifndef _NOISEBRIDGE_VERSION_H
#define _NOISEBRIDGE_VERSION_H

#include <dmr/version.h>

#define NOISEBRIDGE_VERSION_MAJOR    0
#define NOISEBRIDGE_VERSION_MINOR    2
#define NOISEBRIDGE_VERSION_PATCH    25-g125d748
#define NOISEBRIDGE_VERSION_TAG      "git"

#define NOISEBRIDGE_VERSION          __DMR_XSTR(NOISEBRIDGE_VERSION_MAJOR) "." __DMR_XSTR(NOISEBRIDGE_VERSION_MINOR) "." __DMR_XSTR(NOISEBRIDGE_VERSION_PATCH)
#define NOISEBRIDGE_SOFTWARE_ID      "NoiseBridge-" NOISEBRIDGE_VERSION

#endif // _NOISEBRIDGE_VERSION_H