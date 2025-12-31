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
     * We do not use it for resolution here, so we simply skip it.
     */
    if (hdr.linkFlags & HAS_LINK_TARGET_IDLIST) {
        uint16_t idListSize = 0;
        if (fread(&idListSize, sizeof(idListSize), 1, f) != 1) { showError("Bad IDList size"); return 0; }
        if (fseek(f, idListSize, SEEK_CUR) != 0) { showError("Skip IDList failed"); return 0; }
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
