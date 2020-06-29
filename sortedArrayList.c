#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sortedArrayList.h"
void initSortedArrList(struct sortedArrList* s, int (*comparer)(const void *, const void *)){
	s->arrSize=1;
	s->arr=malloc(sizeof(void*)*(s->arrSize));
	s->arrUsed=0;
	s->comparer=comparer;
}
void increaseSizeSortedArrList(struct sortedArrList* s, int c){
	if (s->arrUsed+1>=s->arrSize){
		s->arrSize*=2;
		s->arr=realloc(s->arr,sizeof(void*)*s->arrSize);
	}
}
struct singleDatabaseEntry{
	char* hash;
	char* path;
	signed char seen; // Only used for database
};
// returns index if it's there.
// returns the index it should be at, plus one, times negative one if not.
int searchSortedArr(struct sortedArrList* list, void* val){
	int _leftBound=0;
	int _rightBound=list->arrUsed; // exclusive
	int _cmpRes=0;
	int i=0;
	while(_rightBound>_leftBound){
		i = (_leftBound+_rightBound)/2;
		//printf("%s;%s\n",((struct singleDatabaseEntry*)val)->path,((struct singleDatabaseEntry*)list->arr[i])->path);
		_cmpRes = list->comparer(&val,&(list->arr[i]));
		if (_cmpRes==0){
			return i;
		}else if (_cmpRes>0){
			_leftBound=i+1;
		}else if (_cmpRes<0){
			_rightBound=i;
		}
	}
	return ((_cmpRes>0 ? (i+1) : i)+1)*-1;
}
void shoveInSortedArrList(struct sortedArrList* s, void* val){
	int _index = searchSortedArr(s,val);
	if (_index>=0){
		fprintf(stderr,"thing is already in list!\n");
		exit(1);
	}
	_index=(_index*-1)-1;
	indexPutInSortedArrList(s,val,_index);
}
void indexPutInSortedArrList(struct sortedArrList* s, void* val, int _index){
	increaseSizeSortedArrList(s,1);
	memmove(&(s->arr[_index+1]),&(s->arr[_index]),(s->arrUsed-_index)*sizeof(void*));
	s->arr[_index]=val;
	s->arrUsed++;
}
void freeSortedArrList(struct sortedArrList* s){
	free(s->arr);
}
