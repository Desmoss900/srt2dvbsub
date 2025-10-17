#ifndef QC_H
#define QC_H

#include "srt_parser.h"
#include <stdio.h>

// Run QC checks on a single entry and optionally compare with previous.
void qc_check_entry(const char *filename, int index,
                    const SRTEntry *entry, const SRTEntry *prev,
                    FILE *qc);

#endif