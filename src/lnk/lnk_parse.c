/*
 *  LNK parsing (.lnk -> LnkInfo)
 *
 * This compilation unit reads a Windows Shell Link file (.lnk) and extracts
 * the fields we need to:
 *   - build a useful "target path" string
 *   - (optionally) expose metadata for display/debugging
 *
 * Scope:
 *   We are NOT implementing 100% of the .lnk specification here.
 *   We only parse what is needed to open the target in a reasonable way.
 */

#include "open_lnk/lnk.h"

#include "open_lnk/error.h"
#include "open_lnk/lnk_io.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

/*
 * The Shell Link header is a packed on-disk structure.
 * We use packing to match the exact byte layout in the file.
 */
#pragma pack(push,1)
typedef struct {
    uint32_t headerSize;
    uint8_t  clsid[16];
    uint32_t linkFlags;
    uint32_t fileAttributes;
    uint64_t creationTime;
    uint64_t accessTime;
    uint64_t writeTime;
    uint32_t fileSize;
    uint32_t iconIndex;
    uint32_t showCommand;
    uint16_t hotKey;
    uint16_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} ShellLinkHeader;
#pragma pack(pop)

/* LinkFlags bitmask (subset used by this program). */
#define HAS_LINK_TARGET_IDLIST 0x00000001
#define HAS_LINK_INFO          0x00000002
#define HAS_NAME               0x00000004
#define HAS_RELATIVE_PATH      0x00000008
#define HAS_WORKING_DIR        0x00000010
#define HAS_ARGUMENTS          0x00000020
#define HAS_ICON_LOCATION      0x00000040
#define IS_UNICODE             0x00000080

void freeLnkInfo(LnkInfo *li) {
    if (!li) return;
    free(li->localBasePath);
    free(li->localBasePathU);
    free(li->netName);
    free(li->netNameU);
    free(li->deviceName);
    free(li->deviceNameU);
    free(li->idListPath);
    free(li->commonPathSuffix);
    free(li->commonPathSuffixU);
    free(li->nameString);
    free(li->relativePath);
    free(li->workingDir);
    free(li->arguments);
    free(li->iconLocation);

    /* Leave the struct in a clean state if it is reused after freeing. */
    memset(li, 0, sizeof(*li));
}

static int looks_like_drive_path(const char *p) {
    if (!p || strlen(p) < 3) return 0;
    if (!isalpha((unsigned char)p[0])) return 0;
    if (p[1] != ':') return 0;
    if (p[2] != '\\' && p[2] != '/') return 0;
    return 1;
}

static int looks_like_unc_path(const char *p) {
    if (!p || strlen(p) < 5) return 0;
    return ((p[0] == '\\' && p[1] == '\\') || (p[0] == '/' && p[1] == '/'));
}

static int count_drive_segments(const char *p) {
    if (!looks_like_drive_path(p)) return 0;
    const char *s = p + 3; /* after "X:\" */
    int seg = 0;
    int in_seg = 0;
    for (; *s; s++) {
        if (*s == '\\' || *s == '/') {
            if (in_seg) { seg++; in_seg = 0; }
        } else {
            in_seg = 1;
        }
    }
    if (in_seg) seg++;
    return seg;
}

static int count_unc_rest_segments(const char *p) {
    if (!looks_like_unc_path(p)) return 0;

    /* Normalize separators for counting only. */
    int seg = 0;
    const char *s = p;
    while (*s == '\\' || *s == '/') s++;

    /* server */
    while (*s && *s != '\\' && *s != '/') s++;
    if (!*s) return 0;
    while (*s == '\\' || *s == '/') s++;

    /* share */
    while (*s && *s != '\\' && *s != '/') s++;
    if (!*s) return 0;
    while (*s == '\\' || *s == '/') s++;

    /* rest segments after share */
    int in_seg = 0;
    for (; *s; s++) {
        if (*s == '\\' || *s == '/') {
            if (in_seg) { seg++; in_seg = 0; }
        } else {
            in_seg = 1;
        }
    }
    if (in_seg) seg++;
    return seg;
}

static int score_idlist_candidate(const char *s) {
    if (!s || !*s) return -1;
    if (looks_like_unc_path(s)) {
        int rest = count_unc_rest_segments(s);
        return rest * 100 + 50 + (int)(strlen(s) / 8);
    }
    if (looks_like_drive_path(s)) {
        int seg = count_drive_segments(s);
        return seg * 100 + 40 + (int)(strlen(s) / 8);
    }
    return -1;
}

static char *dup_c_string_bounded(const unsigned char *buf, size_t buflen, size_t start, size_t cap) {
    if (!buf || start >= buflen || cap == 0) return NULL;
    size_t max = buflen < start + cap ? buflen : start + cap;

    /*
     * Strings embedded in IDLists are not always NUL-terminated. We stop on:
     * - NUL
     * - control characters (likely binary after the string)
     */
    size_t n = 0;
    while (start + n < max) {
        unsigned char c = buf[start + n];
        if (c == 0) break;
        if (c < 0x20 && c != '\t') break;
        n++;
    }
    if (n == 0) return NULL;

    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, buf + start, n);
    out[n] = 0;
    return out;
}

static char *dup_utf16le_string_bounded(const unsigned char *buf, size_t buflen, size_t start, size_t max_units) {
    if (!buf || start + 2 > buflen) return NULL;
    size_t max_bytes = buflen - start;
    size_t units_avail = max_bytes / 2;
    if (units_avail == 0) return NULL;
    if (max_units > units_avail) max_units = units_avail;
    if (max_units == 0) return NULL;

    /* Copy into uint16_t array so we can reuse lnk_utf16le_to_utf8. */
    uint16_t *w = (uint16_t *)malloc((max_units + 1) * sizeof(uint16_t));
    if (!w) return NULL;
    size_t j = 0;
    for (; j < max_units; j++) {
        uint16_t cu = (uint16_t)buf[start + j * 2] | ((uint16_t)buf[start + j * 2 + 1] << 8);
        w[j] = cu;
        if (cu == 0) break;
    }
    w[max_units] = 0;

    char *out = lnk_utf16le_to_utf8(w, max_units + 1);
    free(w);
    return out;
}

static char *extract_best_path_from_idlist(const unsigned char *buf, size_t buflen) {
    if (!buf || buflen < 4) return NULL;

    char *best = NULL;
    int bestScore = -1;

    /* ASCII scan for strings that look like paths. */
    for (size_t i = 0; i + 4 < buflen; i++) {
        /* Drive letter: "X:\\" */
        if (isalpha(buf[i]) && buf[i + 1] == ':' && (buf[i + 2] == '\\' || buf[i + 2] == '/')) {
            char *cand = dup_c_string_bounded(buf, buflen, i, 4096);
            int sc = score_idlist_candidate(cand);
            if (sc > bestScore) {
                free(best);
                best = cand;
                bestScore = sc;
            } else {
                free(cand);
            }
        }

        /* UNC: "\\\\server\\share" */
        if (buf[i] == '\\' && buf[i + 1] == '\\') {
            char *cand = dup_c_string_bounded(buf, buflen, i, 4096);
            int sc = score_idlist_candidate(cand);
            if (sc > bestScore) {
                free(best);
                best = cand;
                bestScore = sc;
            } else {
                free(cand);
            }
        }
    }

    /* UTF-16LE scan (best-effort; not always aligned). */
    for (size_t i = 0; i + 8 < buflen; i++) {
        /* Drive: 'X' ':' '\\' in UTF-16LE */
        if (isalpha(buf[i]) && buf[i + 1] == 0 &&
            buf[i + 2] == ':' && buf[i + 3] == 0 &&
            (buf[i + 4] == '\\' || buf[i + 4] == '/') && buf[i + 5] == 0) {
            char *cand = dup_utf16le_string_bounded(buf, buflen, i, 4096);
            int sc = score_idlist_candidate(cand);
            if (sc > bestScore) {
                free(best);
                best = cand;
                bestScore = sc;
            } else {
                free(cand);
            }
        }

        /* UNC: '\\' '\\' in UTF-16LE */
        if (buf[i] == '\\' && buf[i + 1] == 0 && buf[i + 2] == '\\' && buf[i + 3] == 0) {
            char *cand = dup_utf16le_string_bounded(buf, buflen, i, 4096);
            int sc = score_idlist_candidate(cand);
            if (sc > bestScore) {
                free(best);
                best = cand;
                bestScore = sc;
            } else {
                free(cand);
            }
        }
    }

    return best;
}

int parse_lnk(FILE *f, LnkInfo *out) {
    if (!f || !out) return 0;
    memset(out, 0, sizeof(*out));

    /*
     * 1) Read and validate the fixed-size ShellLinkHeader.
     * - headerSize must be 0x4C for standard .lnk files.
     * - CLSID must match the Shell Link CLSID.
     */
    ShellLinkHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { showError("Failed to read header"); return 0; }
    if (hdr.headerSize != 0x4C) { showError("Invalid header size"); return 0; }

    /* Expected CLSID: 00021401-0000-0000-C000-000000000046 (Shell Link) */
    static const uint8_t CLSID[16] = {0x01,0x14,0x02,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46};
    if (memcmp(hdr.clsid, CLSID, 16) != 0) { showError("Not a Shell Link file"); return 0; }

    /*
     * Many strings can be stored in Unicode (UTF-16LE) when IS_UNICODE is set.
     * We convert everything to UTF-8 for easier use in the rest of the code.
     */
    int unicode = (hdr.linkFlags & IS_UNICODE) != 0;

    /*
     * 2) Optional LinkTargetIDList:
     * Some shortcuts store the full target path only in the binary IDList.
     * We extract a best-effort path from it as a fallback for resolution.
     */
    if (hdr.linkFlags & HAS_LINK_TARGET_IDLIST) {
        uint16_t idListSize = 0;
        if (fread(&idListSize, sizeof(idListSize), 1, f) != 1) { showError("Bad IDList size"); return 0; }

        unsigned char *idbuf = (unsigned char *)malloc(idListSize);
        if (!idbuf) return 0;
        if (fread(idbuf, 1, idListSize, f) != idListSize) {
            free(idbuf);
            showError("Bad IDList data");
            return 0;
        }
        out->idListPath = extract_best_path_from_idlist(idbuf, idListSize);
        free(idbuf);
    }

    /*
     * 3) Optional LinkInfo structure:
     * This section often contains the most useful information for the target:
     *   - LocalBasePath (ANSI and/or Unicode)
     *   - CommonPathSuffix (ANSI and/or Unicode)
     *
     * The offsets stored in LinkInfo are relative to the start of LinkInfo.
     */
    if (hdr.linkFlags & HAS_LINK_INFO) {
        long li_start = ftell(f);
        uint32_t liSize = 0;
        if (fread(&liSize, 4, 1, f) != 1 || liSize < 0x1C) { showError("Bad LinkInfo size"); return 0; }

        uint32_t liHeaderSize = 0, liFlags = 0;
        uint32_t volOff = 0, lbpOff = 0, cnrlOff = 0, cpsOff = 0;
        if (fread(&liHeaderSize,4,1,f)!=1) { showError("Bad LinkInfo header"); return 0; }
        if (fread(&liFlags,4,1,f)!=1)      { showError("Bad LinkInfo flags");  return 0; }
        if (fread(&volOff,4,1,f)!=1)       { showError("Bad volume offset");   return 0; }
        if (fread(&lbpOff,4,1,f)!=1)       { showError("Bad base offset");     return 0; }
        if (fread(&cnrlOff,4,1,f)!=1)      { showError("Bad CNRL offset");     return 0; }
        if (fread(&cpsOff,4,1,f)!=1)       { showError("Bad suffix offset");   return 0; }

        uint32_t lbpOffU = 0, cpsOffU = 0;
        if (liHeaderSize >= 0x24) {
            if (fread(&lbpOffU,4,1,f)!=1)  { showError("Bad baseU offset");    return 0; }
            if (fread(&cpsOffU,4,1,f)!=1)  { showError("Bad suffixU offset");  return 0; }
        }

        /*
         * Prefer Unicode variants when available; otherwise use ANSI fields.
         *
         * Safety:
         *   We cap reads (lnk_read_* helpers) to avoid extremely large allocations
         *   when the input file is corrupted.
         */
        if (lbpOffU && lbpOffU < liSize) {
            fseek(f, li_start + (long)lbpOffU, SEEK_SET);
            out->localBasePathU = lnk_read_w_string(f, 65535);
        } else if (lbpOff && lbpOff < liSize) {
            fseek(f, li_start + (long)lbpOff, SEEK_SET);
            out->localBasePath = lnk_read_c_string(f, 1u << 20);
        }

        if (cpsOffU && cpsOffU < liSize) {
            fseek(f, li_start + (long)cpsOffU, SEEK_SET);
            out->commonPathSuffixU = lnk_read_w_string(f, 65535);
        } else if (cpsOff && cpsOff < liSize) {
            fseek(f, li_start + (long)cpsOff, SEEK_SET);
            out->commonPathSuffix = lnk_read_c_string(f, 1u << 20);
        }

        /*
         * CommonNetworkRelativeLink (UNC root + optional drive letter)
         *
         * Network shortcuts frequently store the share root in the CNRL structure:
         *   - NetName: "\\\\server\\\\share" (or a variant like "\\server\\share")
         *   - DeviceName: "M:" (mapped network drive)
         *
         * If we don't parse it, the final target may degrade to only the suffix
         * (example: "Video\\aufheben") and become impossible to resolve.
         */
        if (cnrlOff && cnrlOff < liSize) {
            long cn_start = li_start + (long)cnrlOff;
            if (fseek(f, cn_start, SEEK_SET) == 0) {
                uint32_t cnSize = 0;
                if (fread(&cnSize, 4, 1, f) == 1) {
                    /* Basic sanity: header is 0x14 bytes minimum. */
                    if (cnSize >= 0x14 && cnSize <= (liSize - cnrlOff)) {
                        uint32_t cnFlags = 0, netOff = 0, devOff = 0, prov = 0;
                        if (fread(&cnFlags, 4, 1, f) == 1 &&
                            fread(&netOff, 4, 1, f) == 1 &&
                            fread(&devOff, 4, 1, f) == 1 &&
                            fread(&prov, 4, 1, f) == 1) {
                            (void)cnFlags; (void)prov;

                            uint32_t netOffU = 0, devOffU = 0;
                            if (cnSize >= 0x1C) {
                                if (fread(&netOffU, 4, 1, f) != 1) netOffU = 0;
                                if (fread(&devOffU, 4, 1, f) != 1) devOffU = 0;
                            }

                            /* Prefer Unicode NetName when present. */
                            if (!out->netNameU && netOffU && netOffU < cnSize) {
                                if (fseek(f, cn_start + (long)netOffU, SEEK_SET) == 0) {
                                    out->netNameU = lnk_read_w_string(f, 65535);
                                }
                            }
                            if (!out->netName && (!out->netNameU || !*out->netNameU) && netOff && netOff < cnSize) {
                                if (fseek(f, cn_start + (long)netOff, SEEK_SET) == 0) {
                                    out->netName = lnk_read_c_string(f, 1u << 20);
                                }
                            }

                            /* Prefer Unicode DeviceName when present. */
                            if (!out->deviceNameU && devOffU && devOffU < cnSize) {
                                if (fseek(f, cn_start + (long)devOffU, SEEK_SET) == 0) {
                                    out->deviceNameU = lnk_read_w_string(f, 65535);
                                }
                            }
                            if (!out->deviceName && (!out->deviceNameU || !*out->deviceNameU) && devOff && devOff < cnSize) {
                                if (fseek(f, cn_start + (long)devOff, SEEK_SET) == 0) {
                                    out->deviceName = lnk_read_c_string(f, 1u << 20);
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Seek to the end of LinkInfo so the next reads start at the right place. */
        fseek(f, li_start + (long)liSize, SEEK_SET);
    }

    /*
     * 4) Optional StringData fields:
     * These are variable-length entries guarded by LinkFlags bits.
     */
    if (hdr.linkFlags & HAS_NAME)          out->nameString   = lnk_read_string_data(f, unicode);
    if (hdr.linkFlags & HAS_RELATIVE_PATH) out->relativePath = lnk_read_string_data(f, unicode);
    if (hdr.linkFlags & HAS_WORKING_DIR)   out->workingDir   = lnk_read_string_data(f, unicode);
    if (hdr.linkFlags & HAS_ARGUMENTS)     out->arguments    = lnk_read_string_data(f, unicode);
    if (hdr.linkFlags & HAS_ICON_LOCATION) out->iconLocation = lnk_read_string_data(f, unicode);

    return 1;
}
