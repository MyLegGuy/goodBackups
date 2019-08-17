#include <stdio.h>
#include <stdlib.h>

#include "goodLinkedList.h"
struct nList* lowNewnList(){
	struct nList* _ret = malloc(sizeof(struct nList));
	_ret->nextEntry = NULL;
	_ret->data = NULL;
	return _ret;
}
struct nList* getnList(struct nList* _passed, int _index){
	int i=0;
	ITERATENLIST(_passed,{
		if ((i++)==_index){
			return _curnList;
		}
	})
	return NULL;
}
int nListLen(struct nList* _passed){
	if (_passed==NULL){
		return 0;
	}
	int _ret;
	for (_ret=1;(_passed=_passed->nextEntry)!=NULL;++_ret);
	return _ret;
}
void appendnList(struct nList** _source, struct nList* _addThis){
	if (*_source==NULL){
		*_source=_addThis;
		return;
	}
	struct nList* _temp=*_source;
	while(_temp->nextEntry!=NULL){
		_temp=_temp->nextEntry;
	}
	_temp->nextEntry=_addThis;
}
struct nList* addnList(struct nList** _passed){
	struct nList* _addThis = lowNewnList();
	appendnList(_passed,_addThis);
	return _addThis;
}
struct nList* removenList(struct nList** _removeFrom, int _removeIndex){
	if (_removeIndex==0){
		struct nList* _tempHold = *_removeFrom;
		*_removeFrom=_tempHold->nextEntry;
		return _tempHold;
	}
	struct nList* _prev=*_removeFrom;
	int i=1;
	ITERATENLIST(_prev->nextEntry,{
			if (i==_removeIndex){
				_prev->nextEntry = _curnList->nextEntry;
				return _curnList;
			}else{
				_prev=_curnList;
				++i;
			}
		});
	return NULL;
}
void freenListEntry(struct nList* _freeThis, char _freeMemory){
	if (_freeMemory){
		free(_freeThis->data);
	}
	free(_freeThis);
}
void freenList(struct nList* _freeThis, char _freeMemory){
	ITERATENLIST(_freeThis,{
		freenListEntry(_curnList,_freeMemory);
	})
}
struct nList** initSpeedyAddnList(struct nList** _passedList){
	if (*_passedList==NULL){
		return _passedList;
	}
	ITERATENLIST((*_passedList),{
			if (_curnList->nextEntry==NULL){
				return &(_curnList->nextEntry);
			}
		});
	// Should never get to this point
	return NULL;
}
struct nList** speedyAddnList(struct nList** _nextHolder, void* _desiredData){
	// Set the value of our next entry to our new entry
	*_nextHolder = malloc(sizeof(struct nList));
	// Set the data of our new entry
	(*_nextHolder)->data = _desiredData;
	// Now, we're going to point to our new entry's next entry
	return &((*_nextHolder)->nextEntry);
}
void endSpeedyAddnList(struct nList** _nextHolder){
	// Mark the end of the list by adding making it NULL
	*_nextHolder=NULL;
}
// the index you pass will be the index of the inserted element
struct nList* insertnList(struct nList** _passedList, int _index){
	struct nList* _newEntry = malloc(sizeof(struct nList));
	if (*_passedList==NULL || _index==0){
		_newEntry->nextEntry=*_passedList;
		*_passedList=_newEntry;
		return _newEntry;
	}else{
		int i=0;
		ITERATENLIST((*_passedList),{
				if (i==_index-1){
					_newEntry->nextEntry=_curnList->nextEntry;
					_curnList->nextEntry=_newEntry;
					return _newEntry;
				}
				++i;
			});
	}
	return NULL;
}
