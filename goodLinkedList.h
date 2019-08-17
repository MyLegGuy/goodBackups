#ifndef NLISTHEADERINCLUDED
#define NLISTHEADERINCLUDED

#define ITERATENLIST(_passedStart,_passedCode)					\
	{															\
		if (_passedStart!=NULL){								\
			struct nList* _curnList=_passedStart;				\
			struct nList* _cachedNext;							\
			do{													\
				_cachedNext = _curnList->nextEntry;				\
				_passedCode;									\
			}while((_curnList=_cachedNext));					\
		}														\
	}

struct nList{
	struct nList* nextEntry;
	void* data;
};

struct nList* addnList(struct nList** _passed);
void freenList(struct nList* _freeThis, char _freeMemory);
void freenListEntry(struct nList* _freeThis, char _freeMemory);
// Makes an empty node. If you want to make a new list, set your first node to NULL
struct nList* lowNewnList();
int nListLen(struct nList* _passed);
struct nList* getnList(struct nList* _passed, int _index);
struct nList* removenList(struct nList** _removeFrom, int _removeIndex);
struct nList* insertnList(struct nList** _passedList, int _index);
void appendnList(struct nList** _source, struct nList* _addThis);
// Pass the pointer to your dest list. Store the return value in another variable.
struct nList** initSpeedyAddnList(struct nList** _passedList);
// Pass that thingie returned by initSpeedyAddList and then set it to the return value of this
struct nList** speedyAddnList(struct nList** _nextHolder, void* _desiredData);
// Your temp variable thingie is passed to this
void endSpeedyAddnList(struct nList** _nextHolder);
#endif
