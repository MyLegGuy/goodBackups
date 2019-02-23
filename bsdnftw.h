#ifndef GOODBSDHEADERSTUFF
#define GOODBSDHEADERSTUFF
	int nftwArg(const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *, void* customArg), int nfds, int ftwflags, void* customArg);
#endif