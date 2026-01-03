#ifndef OPEN_LNK_CACHE_LINKS_H
#define OPEN_LNK_CACHE_LINKS_H

char* cache_get_prefix_for_lnk(const char *lnk_abs_path);
void  cache_set_prefix_for_lnk(const char *lnk_abs_path, const char *prefix);

#endif
