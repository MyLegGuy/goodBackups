#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"
#include "goodLinkedList.h"
#include "filter.h"
void fixFilter(char* _filter){
	int i;
	int _cachedStrlen = strlen(_filter);
	for (i=0;i<_cachedStrlen;++i){
		switch(_filter[i]){
			case '*':
				_filter[i]=FILTER_WILDCARD;
				break;
			case '\\':
				if (i==_cachedStrlen-1){
					fprintf(stderr,"invalid backslash\n");
					exit(1);
				}
				if (_filter[i+1]=='*' || _filter[i+1]=='\\'){
					memmove(&(_filter[i]),&(_filter[i+1]),_cachedStrlen-i-1);
					_filter[--_cachedStrlen]='\0';
				}else{
					fprintf(stderr,"invalid backslash\n");
					exit(1);
				}
				break;
		}
	}
}
struct filterEntry* loadFilter(const char* _filepath, int* _retLen){
	struct nList* _readExcludes=NULL;
	struct nList** _listAdder = initSpeedyAddnList(&_readExcludes);
	FILE* fp = fopen(_filepath,"rb");
	while(!feof(fp)){
		size_t _readSize=0;
		char* _currentLine=NULL;
		if (getline(&_currentLine,&_readSize,fp)==-1){
			free(_currentLine);
			break;
		}
		if (_currentLine[0]=='#'){ // comments
			free(_currentLine);
			continue;
		}
		removeNewline(_currentLine);
		int _cachedStrlen = strlen(_currentLine);
		if (_cachedStrlen<3){
			free(_currentLine);
			continue;
		}
		if (_currentLine[_cachedStrlen-1]==SEPARATOR){
			_currentLine[_cachedStrlen-1]='\0';
		}
		if (_currentLine[1]!=' '){
			fprintf(stderr,"invalid line format %s\n",_currentLine);
			exit(1);
		}
		struct filterEntry* _curEntry = malloc(sizeof(struct filterEntry));
		_curEntry->flag=0;
		switch(_currentLine[0]){
			case 'F':
				_curEntry->flag=FLAG_FILEPATH;
			case 'f': // file. compare filename
				_curEntry->flag|=FLAG_FILE;
				break;
			case 'D':
				_curEntry->flag=FLAG_FILEPATH;
			case 'd':
				_curEntry->flag|=FLAG_FOLDER;
				break;
			case 'A':
				_curEntry->flag=FLAG_FILEPATH;
			case 'a':
				_curEntry->flag|=FLAG_FILE;
				_curEntry->flag|=FLAG_FOLDER;
				break;
			default:
				fprintf(stderr,"invalid type specifier %c\n",_currentLine[0]);
				exit(1);
				break;
		}
		fixFilter(_currentLine+2);
		_curEntry->pattern=_currentLine+2;
		_listAdder = speedyAddnList(_listAdder,_curEntry);
	}
	fclose(fp);
	endSpeedyAddnList(_listAdder);
	// convert to array
	*_retLen=nListLen(_readExcludes);
	struct filterEntry* _retFilters;
	_retFilters = malloc(sizeof(struct filterEntry)*(*_retLen));
	int i=0;
	ITERATENLIST(_readExcludes,{
			memcpy(&_retFilters[i++],_curnList->data,sizeof(struct filterEntry));
		});
	freenList(_readExcludes,1);
	return _retFilters;
}
char filterMatches(const unsigned char* _test, int _testLen, const unsigned char* _filter){
	int _filterPos;
	int _testPos;
	int _filterLen=strlen(_filter);
	for (_filterPos=0,_testPos=0;_filterPos<_filterLen && _testPos<_testLen;++_filterPos){
		if (_filter[_filterPos]!=(unsigned char)FILTER_WILDCARD){
			if (_test[_testPos]!=_filter[_filterPos]){
				return 0;
			}
			++_testPos;
		}else{ // process wildcard
			if (_filterPos==_filterLen-1){ // If it's at the end of the string, that means we only needed to match up to here.
				return 1;
			}else{
				// Process wildcard, just jump to the next required character. There should never be two wildcards in a row
				unsigned char* _newString = strchr(_test,_filter[_filterPos+1]);
				if (_newString==NULL){
					return 0;
				}
				_testPos=(_newString-_test)+1;
				++_filterPos; // Because using this wildcard involves matching the next character, go on
			}
		}
	}
	return (_filterPos==_filterLen && _testPos==_testLen);
}
// passed filename length is 0, function may fail
char isFiltered(const char* _passedPath, unsigned char _passedType, int _numFilters, struct filterEntry* _filters){
	const char* _asFilename=_passedPath;
	const char* _loopPath;
	for (_loopPath=_passedPath;_loopPath[1]!='\0';++_loopPath){ // Loop stops one character away from the end. avoid folder end slash
		if (_loopPath[0]==SEPARATOR){
			_asFilename = &(_loopPath[1]);
		}
	}
	int _asFilenameLen = strlen(_asFilename);
	int _passedLen = strlen(_passedPath);
	// Strip end slash of passed folder name
	if (_passedPath[_passedLen-1]==SEPARATOR){
		--_passedLen;
		--_asFilenameLen;
	}
	int i;
	for (i=0;i<_numFilters;++i){
		if (_passedType & _filters[i].flag){ // if file type matches
			if (_filters[i].flag & FLAG_FILEPATH){
				if (filterMatches(_passedPath,_passedLen,_filters[i].pattern)){
					return 1;
				}
			}else{
				if (filterMatches(_asFilename,_asFilenameLen,_filters[i].pattern)){
					return 1;
				}
			}
		}
	}
	return 0;
}
