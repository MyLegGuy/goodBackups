// These must all use characters unused by utf8
#define FILTER_WILDCARD 0xFF // Note that star supports matching 0 characters.
//
#define FLAG_FILE 1
#define FLAG_FOLDER 2
#define FLAG_FILEPATH 4 // otherwise it's filename only
//
struct filterEntry{
	char* pattern;
	unsigned char flag;
};

void fixFilter(char* _filter);
struct filterEntry* loadFilter(const char* _filepath, int* _retLen);
char filterMatches(const unsigned char* _test, int _testLen, const unsigned char* _filter);
char isFiltered(const char* _passedPath, unsigned char _passedType, int _numFilters, struct filterEntry* _filters);
