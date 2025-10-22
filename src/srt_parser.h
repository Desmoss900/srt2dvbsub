
#ifndef SRT_PARSER_H
#define SRT_PARSER_H

#include <stdio.h>
#include <stdint.h>


typedef struct {
    int64_t start_ms, end_ms;
    char *text;
    int alignment;   // parsed {\anX} code
} SRTEntry;

int parse_srt(const char *filename, SRTEntry **entries_out, FILE *qc);

// Convert HTML-like tags (<i>, <b>, <font color>) to ASS markup
char* srt_html_to_ass(const char *in);

// Remove all ASS/HTML tags for length/QC calculations
char* strip_tags(const char *in);

#endif