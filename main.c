// TODO - Put summary at end of stdout
// TODO - files with line breaks in their names dont work. use null as delim?

#define _XOPEN_SOURCE 500 // enable nftw
#define _GNU_SOURCE // for getline
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <dirent.h> // for open dir
#include <errno.h>
//
#include <zlib.h> // for crc32
//
#include "bsdnftw.h"
#include "goodLinkedList.h"
#include "main.h"
#include "filter.h"
#include "sortedArrayList.h"
//
#define HASHBUFFSIZE 16000 // 16 k
#define COPYBUFF HASHBUFFSIZE
#define DATABASESEPARATOR ' '
#define HASHLEN 8
#define HASHLENSTR "8"
#define OKMESSAGE "OK"
#define BADMESSAGE "OHNO"
#define REPORTMESSAGE "%s: got %s, expected %s\n\t%s\n"
#define ADDNEWMESSAGE "New file:\n\t%s\n"
#define ADDNEWWITHHASHMESSAGE "New file with tag:\n\t%s\n"
//
#define ACTION_CHECKEXISTING 1 // Check files that are already in database
#define ACTION_COPYMISSING 2 // Copy files that weren't in the folder, but were in the database
#define ACTION_UPDATEDB 4 // New files that aren't in the database are hashed and added. Also checked if they have a tag.
#define ACTION_LISTMISSING 8
//
struct singleDatabaseEntry{
	char* hash;
	char* path;
	signed char seen; // Only used for database
	signed char isNew;
};
struct checkArg{
	char* rootChop;
	struct sortedArrList* database;
	struct nList* retBad;
	char hasChangedDatabase;
	long chosenActions;
	int numIncludes; // -1 to disable
	struct filterEntry* includeFilters;
	int numExcludes; // -1 to disable
	struct filterEntry* excludeFilters;
	struct sortedArrList* symList;
	signed char hasChangedSymList; // is -1 if sym links are not being saved
};
struct linkPair{
	char* source;
	char* dest;
};
//
int entryPointerComparer(const void* a, const void* b){
	char* _strA = ((struct singleDatabaseEntry*)*((void**)a))->path;
	char* _strB = ((struct singleDatabaseEntry*)*((void**)b))->path;
	return strcmp(_strA,_strB);
}
int linkPairPtrComparer(const void* a, const void* b){
	char* _strA = ((struct linkPair*)*((void**)a))->source;
	char* _strB = ((struct linkPair*)*((void**)b))->source;
	return strcmp(_strA,_strB);
}
char fileExists(const char* _passedPath){
	return (access(_passedPath, R_OK)!=-1);
}
char dirExists(const char* _passedPath){
	DIR* _tempDir = opendir(_passedPath);
	if (_tempDir!=NULL){
		closedir(_tempDir);
		return 1;
	}else/* if (ENOENT == errno)*/{
		return 0;
	}
}
const char* findCharBackwards(const char* _startHere, const char* _endHere, int _target){
	do{
		if (_startHere[0]==_target){
			return _startHere;
		}
		--_startHere;
	}while(_startHere>_endHere);
	return NULL;
}
void removeNewline(char* _toRemove){
	int _cachedStrlen = strlen(_toRemove);
	if (_cachedStrlen==0){
		return;
	}
	if (_toRemove[_cachedStrlen-1]==0x0A){ // Last char is UNIX newline
		if (_cachedStrlen>=2 && _toRemove[_cachedStrlen-2]==0x0D){ // If it's a Windows newline
			_toRemove[_cachedStrlen-2]='\0';
		}else{ // Well, it's at very least a UNIX newline
			_toRemove[_cachedStrlen-1]='\0';
		}
	}
}
void writeSymDatabase(struct sortedArrList* _passedSymList, char* _passedOut){
	FILE* fp = fopen(_passedOut,"wb");
	if (fp==NULL){
		fprintf(stderr,"Failed to open %s;%s\n",_passedOut,strerror(errno));
		return;
	}
	//
	for (int j=0;j<_passedSymList->arrUsed;++j){
		struct linkPair* _currentEntry = _passedSymList->arr[j];
		// add one to the length in order to also write null byte
		int _cachedStrlen=strlen(_currentEntry->source)+1;
		if (fwrite(_currentEntry->source,1,_cachedStrlen,fp)!=_cachedStrlen){
			fputs("failed to write correct number of bytes",stderr);
			break;
		}
		_cachedStrlen=strlen(_currentEntry->dest)+1;
		if (fwrite(_currentEntry->dest,1,_cachedStrlen,fp)!=_cachedStrlen){
			fputs("failed to write correct number of bytes",stderr);
			break;
		}
	}
	fclose(fp);
}
void writeDatabase(struct sortedArrList* _passedDatabase, char* _passedOut){
	char _isStdout=0;
	FILE* fp = fopen(_passedOut,"wb");
	if (fp==NULL){
		fprintf(stderr,"Failed to open %s\n",_passedOut);
		char* _possibleNewFilename = strdup(tmpnam(NULL));
		if (_possibleNewFilename==NULL){
			fprintf(stderr,"Couldn't make temp filename.");
		}else{
			fp = fopen(_possibleNewFilename,"wb");
			if (fp==NULL){
				fprintf(stderr,"Failed to open temp file at %s\n",_possibleNewFilename);
			}else{
				fprintf(stderr,"Writing to temp file at %s\n",_possibleNewFilename);
			}
		}
		free(_possibleNewFilename);
		if (fp==NULL){
			fprintf(stderr,"Falling back on writing database to stderr.");
			fp = stderr;
			_isStdout=1;
			//ITERATENLIST(_passedDatabase,{
			//	struct singleDatabaseEntry* _currentEntry = _curnList->data;
			//	printf("%s %s\n",_currentEntry->nane,_currentEntry->hash);
			//})
			//return;
		}
	}
	//
	for (int i=0;i<_passedDatabase->arrUsed;++i){
		struct singleDatabaseEntry* _currentEntry = _passedDatabase->arr[i];
		if (fprintf(fp,"%s %s\n",_currentEntry->path,_currentEntry->hash)!=(strlen(_currentEntry->path)+1+strlen(_currentEntry->hash)+strlen("\n"))){
			fprintf(stderr,"wrote wrong number of bytes\n");
		}
	}
	//
	if (!_isStdout){
		fclose(fp);
	}
}
struct sortedArrList* readSymDatabase(char* _infile){
	FILE* fp = fopen(_infile,"rb");
	if (fp==NULL){
		fprintf(stderr,"could not open for reading %s\n",_infile);
		exit(1);
	}
	struct sortedArrList* _ret = malloc(sizeof(struct sortedArrList));
	initSortedArrList(_ret,linkPairPtrComparer);
	char* _curLine=NULL;
	size_t _buffSize=0;
	while(1){
		if (getdelim(&_curLine,&_buffSize,'\0',fp)==-1){
			if (feof(fp)){
				break;
			}else{
				fputs("error reading from file (simA)",stderr);
				exit(1);
			}
		}
		struct linkPair* _newEntry = malloc(sizeof(struct linkPair));
		_newEntry->source=strdup(_curLine);
		if (getdelim(&_curLine,&_buffSize,'\0',fp)==-1){
			fputs("error reading from file (simB)",stderr);
			exit(1);
		}
		_newEntry->dest=strdup(_curLine);
		shoveInSortedArrList(_ret,_newEntry);
	}
	free(_curLine);
	fclose(fp);
	return _ret;
}
struct sortedArrList* readDatabase(char* _passedDatabaseFile){
	FILE* fp = fopen(_passedDatabaseFile,"rb");
	if (fp==NULL){
		fprintf(stderr,"Could not open for reading %s\n",_passedDatabaseFile);
		return NULL;
	}
	struct sortedArrList* _retList = malloc(sizeof(struct sortedArrList));
	initSortedArrList(_retList,entryPointerComparer);
	while(!feof(fp)){
		size_t _lastRead=0;
		char* _currentLine=NULL;
		if (getline(&_currentLine,&_lastRead,fp)==-1){
			free(_currentLine);
			break;
		}
		removeNewline(_currentLine);
		if (strlen(_currentLine)<=1){ // Empty line
			free(_currentLine);
			continue;
		}

		//char* _spaceSpot = strchr(_currentLine,DATABASESEPARATOR);
		const char* _spaceSpot = findCharBackwards(&(_currentLine[strlen(_currentLine)-1]),_currentLine,DATABASESEPARATOR);
		if (_spaceSpot==NULL){
			fprintf(stderr,"corrupted database line %s\n",_currentLine);
			continue;
		}
		struct singleDatabaseEntry* _currentEntry = malloc(sizeof(struct singleDatabaseEntry));
		_currentEntry->seen=0;
		_currentEntry->isNew=0;

		_currentEntry->path = malloc(_spaceSpot-_currentLine+1);
		memcpy(_currentEntry->path,_currentLine,_spaceSpot-_currentLine);
		_currentEntry->path[_spaceSpot-_currentLine]='\0';
		
		_currentEntry->hash = malloc(strlen(_spaceSpot));
		strcpy(_currentEntry->hash,_spaceSpot+1);

		shoveInSortedArrList(_retList,_currentEntry);
		free(_currentLine);
	}
	fclose(fp);
	// TODO - somehow figure out if the database is already sorted so that I don't need to do this?
	qsort(_retList->arr,_retList->arrUsed,sizeof(void*),entryPointerComparer);
	return _retList;
}
void freeDatabase(struct sortedArrList* _passedList){
	for (int i=0;i<_passedList->arrUsed;++i){
		free(((struct singleDatabaseEntry*)_passedList->arr[i])->path);
		free(((struct singleDatabaseEntry*)_passedList->arr[i])->hash);
		free(_passedList->arr[i]);
	}
	freeSortedArrList(_passedList);
	free(_passedList);
}
void resetDatabaseSeen(struct sortedArrList* _passedList){
	for (int i=0;i<_passedList->arrUsed;++i){
		((struct singleDatabaseEntry*)_passedList->arr[i])->seen=0;
	}
}
void freeSymDatabase(struct sortedArrList* _passedList){
	for (int j=0;j<_passedList->arrUsed;++j){
		struct linkPair* _currentEntry = _passedList->arr[j];
		free(_currentEntry->source);
		free(_currentEntry->dest);
		free(_currentEntry);
	}
	free(_passedList->arr);
	free(_passedList);
}
char readABit(FILE* fp, char* _destBuffer, long* _numRead, long _maxRead){
	if (feof(fp)){
		return 1;
	}
	*_numRead = fread(_destBuffer,1,_maxRead,fp);
	return 0;
}
char lowCopyFile(const char* _srcPath, const char* _destPath, char _canMakeDirs){
	FILE* _destfp = fopen(_destPath,"wb");
	if (_destfp!=NULL){
		FILE* _sourcefp = fopen(_srcPath,"rb");
		if (_sourcefp!=NULL){
			char* _currentBit = malloc(COPYBUFF);
			size_t _lastRead;
			while (!readABit(_sourcefp,_currentBit,&_lastRead,COPYBUFF)){
				if (fwrite(_currentBit,1,_lastRead,_destfp)!=_lastRead){
					fprintf(stderr,"wrote wrong number of bytes.\n");
				}
			}
			free(_currentBit);
			fclose(_sourcefp);
		}else{
			fprintf(stderr,"Failed to open for reading %s\n",_srcPath);
		}
		fclose(_destfp);
	}else{
		// Make all directories that need to be made for the destination to work
		char _shouldRetry=0;
		if (_canMakeDirs){
			char* _tempPath = strdup(_destPath);
			int _numMakeDirs=0;
			while(1){
				char* _possibleSeparator=(char*)findCharBackwards(&(_tempPath[strlen(_tempPath)-1]),_tempPath,SEPARATOR);
				if (_possibleSeparator!=NULL && _possibleSeparator!=_tempPath){
					_possibleSeparator[0]='\0';
					if (dirExists(_tempPath)){ // When the directory that does exist is found break to create the missing ones in order.
						break;
					}else{
						++_numMakeDirs;
					}
				}else{
					break;
				}
			}
			if (_numMakeDirs>0){
				_shouldRetry=1;
				int i;
				for (i=0;i<_numMakeDirs;++i){
					_tempPath[strlen(_tempPath)]=SEPARATOR;
					if (mkdir(_tempPath,0777)==0){
						printf("Make directory: %s\n",_tempPath);
					}else{
						fprintf(stderr,"Failed to make directory %s\n",_tempPath);
					}
				}
			}
			free(_tempPath);
		}

		if (_shouldRetry){
			return lowCopyFile(_srcPath,_destPath,0);
		}else{
			fprintf(stderr,"Failed to open for writing %s\n",_destPath);
			return 1;
		}
	}
	return 0;
}
char copyFile(const char* _srcPath, const char* _destPath){
	return lowCopyFile(_srcPath,_destPath,1);
}
unsigned char* readEntireFile(char* _filename, long* _retSize){
	char* _loadedBuffer;
	FILE* fp = fopen(_filename, "rb");
	if (fp==NULL){
		return NULL;
	}
	// Get file size
	fseek(fp, 0, SEEK_END);
	long _foundFilesize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	// Read file into memory
	_loadedBuffer = malloc(_foundFilesize);
	fread(_loadedBuffer, _foundFilesize, 1, fp);
	fclose(fp);
	*_retSize = _foundFilesize;
	return _loadedBuffer;
}
char* getTagHash(const char* _passedFilename, int _fakeStrlen){
	const char* _possibleHashStart = findCharBackwards(_passedFilename+_fakeStrlen-1,_passedFilename, '[');
	if (_possibleHashStart!=NULL && _possibleHashStart>strchr(_passedFilename, SEPARATOR)){
		++_possibleHashStart; // Skip past left bracket
		char* _possibleHashEnd = strchr(_possibleHashStart, ']');
		if (_possibleHashEnd!=NULL){
			int _hashLen = _possibleHashEnd-_possibleHashStart;
			char _isPotentialHash = (_hashLen==HASHLEN);
			if (_isPotentialHash){
				// Make sure it only has hex chars
				int j;
				for (j=0;j<HASHLEN;++j){
					if(!((_possibleHashStart[j]>='0' && _possibleHashStart[j]<='9') || (_possibleHashStart[j]>='A' && _possibleHashStart[j]<='F'))){
						_isPotentialHash=0;
						break;
					}
				}
			}
			if (!_isPotentialHash){
				--_possibleHashStart;
				return getTagHash(_passedFilename,_possibleHashStart-_passedFilename);
			}else{
				char* _foundHash = malloc(_hashLen+1);
				memcpy(_foundHash,_possibleHashStart,_hashLen);
				_foundHash[_hashLen]='\0';
				return _foundHash;
			}
		}
	}
	return NULL;
}
char* hashFile(const char* _passedFilename){
	FILE* fp = fopen(_passedFilename,"rb");
	if (fp!=NULL){
		char* _lastBuf = malloc(HASHBUFFSIZE);
		uLong _finalHash = crc32(0L, Z_NULL, 0);
		size_t _numRead;
		while(!readABit(fp,_lastBuf,&_numRead,HASHBUFFSIZE)){
			_finalHash = crc32(_finalHash, _lastBuf, _numRead);
		}
		fclose(fp);
		free(_lastBuf);
		char* _ret = malloc(HASHLEN+1);
		sprintf(_ret,"%0"HASHLENSTR"lX",_finalHash);
		return _ret;
	}else{
		fprintf(stderr,"Failed to open %s\n",_passedFilename);
		return NULL;
	}
}
int getFromSymList(struct sortedArrList* _passedList, const char* _strippedName){
	struct linkPair _testEntry;
	_testEntry.source=(char*)_strippedName;
	return searchSortedArr(_passedList,&_testEntry);
}
int getFromDatabase(struct sortedArrList* _passedDatabase, const char* _searchName){
	struct singleDatabaseEntry _testEntry;
	_testEntry.path=(char*)_searchName;
	return searchSortedArr(_passedDatabase,&_testEntry);
}
struct singleDatabaseEntry* getFromDatabaseItem(struct sortedArrList* _passedDatabase, const char* _searchName){
	int _index = getFromDatabase(_passedDatabase,_searchName);
	if (_index>=0){
		return _passedDatabase->arr[_index];
	}
	return NULL;
}
// sometimes a passed folder will end with a slash, but not always
int checkSingleFile(const char *fpath, const struct stat *sb, int typeflag, struct FTW* ftwbuf, void* _arg){
	struct checkArg* _passedCheck = _arg;
	int _cachedRootStrlen = strlen(_passedCheck->rootChop);
	if (strncmp(fpath,_passedCheck->rootChop,_cachedRootStrlen)!=0){
		fprintf(stderr,"Bad root to path. Path is: %s root is %s\n",fpath,_passedCheck->rootChop);
		return 1;
	}
	unsigned char _filterTypePass;
	if (typeflag==FTW_F){
		_filterTypePass=FLAG_FILE;
	}else{
		if (typeflag==FTW_D){
			_filterTypePass=FLAG_FOLDER;
		}else{
			// For unknown stuff, try to match it with any filters
			_filterTypePass=(FLAG_FILE | FLAG_FOLDER);
		}
	}
	// Process includes and excludes if used
	char _isQuit=0;
	if (_passedCheck->numIncludes!=-1){
		if (!isFiltered(fpath,_filterTypePass,_passedCheck->numIncludes,_passedCheck->includeFilters)){
			printf("Not included: %s\n",fpath);
			_isQuit=1;
		}
	}
	if (!_isQuit && _passedCheck->numExcludes!=-1){
		if (isFiltered(fpath,_filterTypePass,_passedCheck->numExcludes,_passedCheck->excludeFilters)){
			printf("Excluded: %s\n",fpath);
			_isQuit=1;
		}
	}
	if (_isQuit){
		if (typeflag==FTW_D){
			return 2; // Skip dir contents
		}else{
			return 0; // for unknown things and files - return OK
		}
	}
	//
	if (typeflag==FTW_F){
		char* _actualHash = NULL;
		if (_passedCheck->chosenActions & ACTION_CHECKEXISTING){
			_actualHash = hashFile(fpath);
		}
		if (_actualHash!=NULL || ((_passedCheck->chosenActions & ACTION_CHECKEXISTING)==0)){
			signed char _fileMatched=-1; // -1 means not in database, 0 means did not match, 1 means matched, 2 means do no more with this file			
			int _index = getFromDatabase(_passedCheck->database,&(fpath[_cachedRootStrlen]));
			if (_index>=0){
				struct singleDatabaseEntry* _matchingEntry=_passedCheck->database->arr[_index];
				if (_matchingEntry->seen){
					fprintf(stderr,"What? Already seen %s?\n",_matchingEntry->path);
				}
				_matchingEntry->seen=1;
				if (_passedCheck->chosenActions & ACTION_CHECKEXISTING){ // Hash must exist to get here
					_fileMatched=(strcmp(_actualHash,_matchingEntry->hash)==0);
					fprintf(_fileMatched ? stdout : stderr,REPORTMESSAGE,_fileMatched ? OKMESSAGE : BADMESSAGE, _actualHash, _matchingEntry->hash, fpath);
				}else{
					_fileMatched=2;
				}
			}else{
				_index=(_index*-1)-1;
			}
			if (_fileMatched==-1 || _fileMatched==0){ // File not in database or file wrong hash.
				struct singleDatabaseEntry* _newEntry = malloc(sizeof(struct singleDatabaseEntry));
				_newEntry->path = strdup(&(fpath[_cachedRootStrlen]));
				_newEntry->seen = 1;
				_newEntry->hash = "Nothing should be here right now.";
				if (_fileMatched==0){ // File wrong hash. Only happens for ACTION_CHECKEXISTING
					_newEntry->isNew=0;
					_newEntry->hash = strdup(_actualHash);
					addnList(&(_passedCheck->retBad))->data=_newEntry;
				}else if (_fileMatched==-1 && (_passedCheck->chosenActions & ACTION_UPDATEDB)){ // File not found in database then add it
					_newEntry->isNew=1;
					if (_actualHash==NULL){
						_actualHash = hashFile(fpath);
					}
					if (_actualHash!=NULL){
						char* _possibleTagHash = getTagHash(fpath,strlen(fpath));
						if (_possibleTagHash!=NULL){ // Not in the database, but the filename has a hash we can use.
							//printf(ADDNEWWITHHASHMESSAGE,_newEntry->path);
							_fileMatched=(strcmp(_actualHash,_possibleTagHash)==0);
							fprintf(_fileMatched ? stdout : stderr,REPORTMESSAGE,_fileMatched ? OKMESSAGE : BADMESSAGE, _actualHash, _possibleTagHash, fpath);
							if (_fileMatched){
								// Can reuse our struct because our file matched so we don't need to add to bad list
								_newEntry->hash = _possibleTagHash;
								indexPutInSortedArrList(_passedCheck->database,_newEntry,_index);
							}else{
								// Need a new struct for the database representing the okay hash
								struct singleDatabaseEntry* _addThis = malloc(sizeof(struct singleDatabaseEntry));
								_addThis->path = strdup(_newEntry->path);
								_addThis->hash = _possibleTagHash;
								_addThis->seen = 1;
								_addThis->isNew = 1;
								indexPutInSortedArrList(_passedCheck->database,_addThis,_index);
								// and it's bad
								_newEntry->hash = strdup(_actualHash);
								addnList(&(_passedCheck->retBad))->data=_newEntry;
							}
						}else{ // No hash, not in database, assume our file is okay
							printf(ADDNEWMESSAGE,fpath);
							_newEntry->hash = strdup(_actualHash);
							indexPutInSortedArrList(_passedCheck->database,_newEntry,_index);
						}
						_passedCheck->hasChangedDatabase=1;
					}else{
						fprintf(stderr,"(way2)Failed to hash file %s\n",fpath);
					}
				}else{
					free(_newEntry->path);
					free(_newEntry);
					printf("Do nothing with file %s not in database.\n",fpath);
				}
			}
		}else{
			fprintf(stderr,"(way1)Failed to hash file %s\n",fpath);
		}
		free(_actualHash);
	}else if ((typeflag==FTW_SL || typeflag==FTW_SLN) && (_passedCheck->hasChangedSymList!=-1)){ // if it's a symlink and symlink saving is enabled
		const char* _strippedPath = &(fpath[_cachedRootStrlen]);
		int _index = getFromSymList(_passedCheck->symList,_strippedPath);
		if (_index<0){
			_index=(_index*-1)-1;
			char* _destPath=realpath(fpath,NULL); // is allocated automatically
			if (_destPath){
			putlink:
				_passedCheck->hasChangedSymList=1;
				struct linkPair* _addEntry = malloc(sizeof(struct linkPair));
				_addEntry->source=strdup(_strippedPath);
				_addEntry->dest=_destPath;
				indexPutInSortedArrList(_passedCheck->symList,_addEntry,_index);
			}else{
				if (errno==ENOENT){ // dest does not exist
					_destPath=strdup("//broken//");
					goto putlink;
				}
				fprintf(stderr,"failed to follow link %s: %s\n",fpath,strerror(errno));
				return 1;
			}
		}
	}else{
		if (typeflag!=FTW_D){
			if (typeflag==FTW_SLN){
				fprintf(stderr,"Warning: FTW_SLN cannot be saved without symlink list.\n%s\n",fpath);
			}else{
				fprintf(stderr,"Unknown thing passed.\n%d:%s\n",typeflag,fpath);
				return 1;
			}
		}
	}
	return 0;
}
// Returns list of broken files
char checkDir(struct sortedArrList* _passedDatabase, char* _passedDirectory, char* _ret_DatabaseModified, long _passedActions, int _numIncludes, struct filterEntry* _passedIncludes, int _numExcludes, struct filterEntry* _passedExcludes, struct nList** _retBad, struct sortedArrList* _passedSymList, char* _retChangedSymList){
	struct checkArg myCheckArgs;
	myCheckArgs.database = _passedDatabase;
	myCheckArgs.retBad = NULL; 
	myCheckArgs.rootChop = _passedDirectory;
	myCheckArgs.hasChangedDatabase=0;
	myCheckArgs.chosenActions = _passedActions;
	myCheckArgs.numIncludes=_numIncludes;
	myCheckArgs.includeFilters=_passedIncludes;
	myCheckArgs.numExcludes=_numExcludes;
	myCheckArgs.excludeFilters=_passedExcludes;
	if (_passedSymList!=NULL){
		myCheckArgs.hasChangedSymList=0;
		myCheckArgs.symList=_passedSymList;
	}else{
		myCheckArgs.hasChangedSymList=-1; // disable sym list
		myCheckArgs.symList=NULL;
	}
	char _ret = nftwArg(_passedDirectory,checkSingleFile,5,_passedSymList!=NULL ? FTW_PHYS : 0, &myCheckArgs);
	*_ret_DatabaseModified = myCheckArgs.hasChangedDatabase;
	*_retBad = myCheckArgs.retBad;
	if (_passedSymList!=NULL){
		*_retChangedSymList=myCheckArgs.hasChangedSymList;
	}
	return _ret;
}
char hasArg(char* _searchTarget, int argc, char** args){
	int i;
	for (i=0;i<argc;++i){
		if (strcmp(args[i],_searchTarget)==0){
			return i+1;
		}
	}
	return 0;
}
int main(int argc, char** args){
	if (argc<3){
		printf("need more args.\n%s <db file> <primary folder> [backup folder 1] [backup folder ...] [optional args]\n",args[0]);
		return 0;
	}
	int i;

	int _numIncludeFilters=-1;
	struct filterEntry* _includeFilters=NULL;
	int _numExcludeFilters=-1;
	struct filterEntry* _excludeFilters=NULL;

	char* _symListFile=NULL;
	
	int _numFolders=argc-2;
	long _passedActions = 0;
	char _addFromPrimaryOnly=1;
	char _primaryCanRestoreMissing=0;
	char _missingCanBeOldFile=0;
	if (access(args[1],F_OK)==-1){ // If our target database doesn't exist
		if (hasArg("--newdb",argc,args)){
			--_numFolders;
			printf("Making new database...\n");
			FILE* fp = fopen(args[1],"wb");
			if (fp!=NULL){
				fclose(fp);
			}else{
				fprintf(stderr,"Failed to create newfile at %s\n",args[1]);
				return 1;
			}
			printf("Made new database.\n");
		}else{
			fprintf(stderr,"%s does not exist. To make a new database, pass --newdb\n",args[1]);
			return 1;
		}
	}
	if (access(args[1],W_OK)==-1){
		fprintf(stderr,"can't write to database file at %s\n",args[1]);
		return 1;
	}
	//
	if (hasArg("--full",argc,args)){
		_passedActions |= ACTION_COPYMISSING;
		_passedActions |= ACTION_CHECKEXISTING;
		_passedActions |= ACTION_UPDATEDB;
		--_numFolders;
	}else if (hasArg("--update",argc,args)){
		_passedActions |= ACTION_COPYMISSING;
		_passedActions |= ACTION_UPDATEDB;
		--_numFolders;
	}
	if (hasArg("--listMissing",argc,args)){
		_passedActions |= ACTION_LISTMISSING;
		--_numFolders;
	}
	if (hasArg("--addFromBackups",argc,args)){
		--_numFolders;
		_addFromPrimaryOnly=0;
	}
	if (hasArg("--primaryCanRestoreMissing",argc,args)){
		--_numFolders;
		_primaryCanRestoreMissing=1;
	}
	if (hasArg("--missingCanBeOldFile",argc,args)){
		--_numFolders;
		_missingCanBeOldFile=1;
	}
	int _possibleIndex = hasArg("--include",argc,args);
	if (_possibleIndex){
		_numFolders-=2;
		_includeFilters = loadFilter(args[_possibleIndex], &_numIncludeFilters);
	}
	if ((_possibleIndex=hasArg("--exclude",argc,args))){
		_numFolders-=2;
		_excludeFilters = loadFilter(args[_possibleIndex], &_numExcludeFilters);
	}
	if ((_possibleIndex=hasArg("--symSave",argc,args))){
		_numFolders-=2;
		_symListFile = args[_possibleIndex];
	}
	//
	for (i=0;i<_numFolders;++i){
		if (args[i+2][strlen(args[i+2])-1]!=SEPARATOR){
			fprintf(stderr,"All folder names need to end with '%c'. Failed on %s\n",SEPARATOR,args[i+2]);
			return 1;
		}
		if (args[i+2][0]=='.'){
			fprintf(stderr,"All folder names should be absolute. Failed on %s\n",args[i+2]);
			return 1;
		}
		if (!dirExists(args[i+2])){
			fprintf(stderr,"Directory does not exist %s\n",args[i+2]);
			return 1;
		}
	}
	if (_passedActions==0){
		fprintf(stderr,"No actions specified.\n");
		return 1;
	}
	/////////////////
	struct sortedArrList* _curSymList = _symListFile ? readSymDatabase(_symListFile) : NULL;
	struct sortedArrList* _currentDatabase = readDatabase(args[1]);
	struct nList* _brokenLists[_numFolders];
	for (i=0;i<_numFolders;++i){
		fprintf(stderr,"Now checking folder %s\n",args[i+2]);
		printf("Now checking folder %s\n",args[i+2]);
		char _needResaveDatabase=0;
		char _needResaveSymList=0;
		if (checkDir(_currentDatabase,args[i+2],&_needResaveDatabase,_passedActions,_numIncludeFilters,_includeFilters,_numExcludeFilters,_excludeFilters,&_brokenLists[i],_symListFile!=NULL ? _curSymList : NULL,&_needResaveSymList)){
			fprintf(stderr,"Checking failed!\n");
			return 1;
		}
		if (_brokenLists[i]!=NULL){
			fprintf(stderr,"=====\nBROKEN FILES\n======\n");
			ITERATENLIST(_brokenLists[i],{
				struct singleDatabaseEntry* _badEntry = _curnList->data;
				struct singleDatabaseEntry* _expectedEntry = getFromDatabaseItem(_currentDatabase,_badEntry->path);
				if (_expectedEntry!=NULL){
					fprintf(stderr,REPORTMESSAGE,BADMESSAGE, _badEntry->hash, _expectedEntry->hash, _badEntry->path);
				}else{
					fprintf(stderr,"Well, it's broken, but couldn't find the following file in the database:\n\t%s:%s\n",_badEntry->path,_badEntry->hash);
				}
			})
		}else{
			if (!(_passedActions & ACTION_CHECKEXISTING)){
				puts("No new files are broken. :)");
			}else{
				puts("No broken files. :)");
			}
		}
		if (_needResaveDatabase){
			writeDatabase(_currentDatabase,args[1]);
		}else{
			puts("No file database changes.");
		}
		if (_symListFile!=NULL){
			if (_needResaveSymList){
				writeSymDatabase(_curSymList,_symListFile);
			}else{
				puts("No sym database changes.");
			}
		}
		if ((_passedActions & ACTION_COPYMISSING) || (_passedActions & ACTION_LISTMISSING)){
			int _curCheckIndex=0;
			// Look for any files that should've been there but weren't
			for (int j=0;j<_currentDatabase->arrUsed;++j){
				struct singleDatabaseEntry* _currentEntry = _currentDatabase->arr[j];
				if (!_currentEntry->seen){
					if (_currentEntry->isNew || _missingCanBeOldFile){
						char* _destPath = malloc(strlen(_currentEntry->path)+strlen(args[i+2])+1);
						strcpy(_destPath,args[i+2]);
						strcat(_destPath,_currentEntry->path);
						if (_passedActions & ACTION_COPYMISSING && (i!=0 || _primaryCanRestoreMissing)){
							printf("Didn't see %s. Will try and put.\n",_destPath);
							// find a valid copy in another folder
							char _worked=0;
							int j;
							for (j=0;j<_numFolders && !_worked;++j){
								if (j==i){
									continue;
								}
								// get path of a working copy of this file
								char* _tempPath = malloc(strlen(_currentEntry->path)+strlen(args[j+2])+1);
								strcpy(_tempPath,args[j+2]);
								strcat(_tempPath,_currentEntry->path);
								if (fileExists(_tempPath)){
									printf("Copying %s to %s\n",_tempPath,_destPath);
									copyFile(_tempPath,_destPath);
									_worked=1;
								}
								free(_tempPath);
							}
							if (!_worked){
								fprintf(stderr,"Failed to find a copy of %s to copy to %s.\n",_currentEntry->path,_destPath);
							}
						}else{
							fprintf(stderr,"Didn't see %s.\n",_destPath);
						}
						free(_destPath);
					}else{
						fprintf(stderr,"old file (new index %d) is missing: %s\n",_curCheckIndex,_currentEntry->path);
					}
				}
				++_curCheckIndex;
			}
		}
		resetDatabaseSeen(_currentDatabase);
		// if we're only adding to database from the primary folder, disable ACTION_UPDATEDB after the first run
		if (i==0 && _addFromPrimaryOnly){
			_passedActions &= ~(1UL << 2);
		}
	}
	/* for (i=0;i<_numFolders;++i){ */
	/* 	freeDatabase(_brokenLists[i]); */
	/* } */
	freeDatabase(_currentDatabase);
	if (_curSymList){
		freeSymDatabase(_curSymList);
	}
}
