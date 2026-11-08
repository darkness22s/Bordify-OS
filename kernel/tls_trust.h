#ifndef BORDIFY_TLS_TRUST_H
#define BORDIFY_TLS_TRUST_H

#include "bearssl.h"

extern const br_x509_trust_anchor bordify_trust_anchors[];
extern const size_t bordify_trust_anchor_count;

#endif
