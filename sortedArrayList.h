// it's always an array of pointers to something.
// shifting a ton of pointers is faster than shifting a ton of arrays
struct sortedArrList{
	void** arr;
	int arrSize;
	int arrUsed;
	int (*comparer)(const void *, const void *);
};
void initSortedArrList(struct sortedArrList* s, int (*comparer)(const void *, const void *));
void increaseSizeSortedArrList(struct sortedArrList* s, int c);
int searchSortedArr(struct sortedArrList* list, void* val);
void shoveInSortedArrList(struct sortedArrList* s, void* val);
void indexPutInSortedArrList(struct sortedArrList* s, void* val, int _index);
void freeSortedArrList(struct sortedArrList* s);
