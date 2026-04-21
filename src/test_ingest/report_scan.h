/*
 * report_scan.h — Discover JUnit XML report files under conventional build directories.
 */
#ifndef CBM_REPORT_SCAN_H
#define CBM_REPORT_SCAN_H

/* Scan well-known report locations under `cwd` and return absolute paths to `.xml` files.
 * Returns 0 on success with `*out_xml_files` / `*out_count` (possibly zero files).
 * Returns -1 when `cwd` cannot be resolved or none of the standard report directories exist.
 * On -1, `*out_xml_files` is NULL and `*out_count` is 0.
 */
int cbm_scan_report_dirs(const char *cwd, char ***out_xml_files, int *out_count);

void cbm_scan_report_dirs_free(char **files, int count);

#endif /* CBM_REPORT_SCAN_H */
