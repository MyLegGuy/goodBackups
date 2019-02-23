#define _XOPEN_SOURCE 500 // enable nftw
#define _GNU_SOURCE // for getline
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
//
#include <zlib.h> // for crc32
//
#include "bsdnftw.h"
#include "goodLinkedList.h"
//
#define HASHBUFFSIZE 16000 // 16 k
#define COPYBUFF HASHBUFFSIZE
#define SEPARATOR '/'
#define DATABASESEPARATOR ' '
#ifndef linux
	#warning forward slash is used for directory separator
#endif
#define FOLLOWSYMS 0
#define HASHLEN 8
#define OKMESSAGE "OK"
#define BADMESSAGE "OHNO"
#define REPORTMESSAGE "%s: got %s, expected %s\n\t%s\n"
#define ADDNEWMESSAGE "New file:\n\t%s\n"
//
struct singleDatabaseEntry{
	char* hash;
	char* path;
};
struct checkArg{
	char* rootChop;
	nList* database;
	nList* retBad;
	char hasChangedDatabase;
};
//
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
void writeDatabase(nList* _passedDatabase, char* _passedOut){
	char _isStdout=0;
	FILE* fp = fopen(_passedOut,"wb");
	if (fp==NULL){
		printf("Failed to open %s\n",_passedOut);
		char* _possibleNewFilename = strdup(tmpnam(NULL));
		if (_possibleNewFilename==NULL){
			printf("Couldn't make temp filename.");
		}else{
			fp = fopen(_possibleNewFilename,"wb");
			if (fp==NULL){
				printf("Failed to open temp file at %s\n",_possibleNewFilename);
			}else{
				printf("Writing to temp file at %s\n",_possibleNewFilename);
			}
		}
		free(_possibleNewFilename);
		if (fp==NULL){
			printf("Falling back on writing to stdout.");
			fp = stdout;
			_isStdout=1;
			//ITERATENLIST(_passedDatabase,{
			//	struct singleDatabaseEntry* _currentEntry = _currentnList->data;
			//	printf("%s %s\n",_currentEntry->nane,_currentEntry->hash);
			//})
			//return;
		}
	}
	//
	ITERATENLIST(_passedDatabase,{
		struct singleDatabaseEntry* _currentEntry = _currentnList->data;
		if (fprintf(fp,"%s %s\n",_currentEntry->path,_currentEntry->hash)!=(strlen(_currentEntry->path)+1+strlen(_currentEntry->hash)+strlen("\n"))){
			printf("wrote wrong number of bytes\n");
		}
	})
	//
	if (!_isStdout){
		fclose(fp);
	}
}
nList* readDatabase(char* _passedDatabaseFile){
	FILE* fp = fopen(_passedDatabaseFile,"rb");
	if (fp==NULL){
		printf("Could not open for reading %s\n",_passedDatabaseFile);
		return NULL;
	}
	nList* _ret = NULL;
	while(!feof(fp)){
		size_t _lastRead=0;
		char* _currentLine=NULL;
		if (getline(&_currentLine,&_lastRead,fp)==-1){
			free(_currentLine);
			break;
		}
		removeNewline(_currentLine);
		if (strlen(_currentLine)<=1){ // Empty line
			continue;
		}

		//char* _spaceSpot = strchr(_currentLine,DATABASESEPARATOR);
		const char* _spaceSpot = findCharBackwards(&(_currentLine[strlen(_currentLine)-1]),_currentLine,DATABASESEPARATOR);
		if (_spaceSpot==NULL){
			printf("corrupted database line %s\n",_currentLine);
			continue;
		}
		struct singleDatabaseEntry* _currentEntry = malloc(sizeof(struct singleDatabaseEntry));
		
		_currentEntry->path = malloc(_spaceSpot-_currentLine+1);
		memcpy(_currentEntry->path,_currentLine,_spaceSpot-_currentLine);
		_currentEntry->path[_spaceSpot-_currentLine]='\0';
		
		_currentEntry->hash = malloc(strlen(_spaceSpot));
		strcpy(_currentEntry->hash,_spaceSpot+1);

		addnList(&_ret)->data = _currentEntry;
		free(_currentLine);
	}
	return _ret;
}
void freeDatabase(nList* _passedList){
	ITERATENLIST(_passedList,{
		free(((struct singleDatabaseEntry*)_currentnList->data)->path);
		free(((struct singleDatabaseEntry*)_currentnList->data)->hash);
		free(_currentnList->data);
		free(_currentnList);
	})
}
char readABit(FILE* fp, char* _destBuffer, long* _numRead, long _maxRead){
	if (feof(fp)){
		return 1;
	}
	*_numRead = fread(_destBuffer,1,_maxRead,fp);
	return 0;
}

void copyFile(const char* _srcPath, const char* _destPath){
	FILE* _sourcefp = fopen(_srcPath,"rb");
	if (_sourcefp!=NULL){
		FILE* _destfp = fopen(_destPath,"wb");
		if (_destfp!=NULL){
			char* _currentBit = malloc(COPYBUFF);
			size_t _lastRead;
			while (!readABit(_sourcefp,_currentBit,&_lastRead,COPYBUFF)){
				if (fwrite(_currentBit,1,_lastRead,_destfp)!=_lastRead){
					printf("wrote wrong number of bytes.\n");
				}
			}
			free(_currentBit);
			fclose(_destfp);
		}else{
			printf("Failed to open for writing %s\n",_destPath);
		}
		fclose(_sourcefp);
	}else{
		printf("Failed to open for reading %s\n",_srcPath);
	}
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
			if (_hashLen!=HASHLEN){
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
		char* _ret = malloc(HASHLEN);
		sprintf(_ret,"%08X",_finalHash);
		return _ret;
	}else{
		printf("Failed to open %s\n",_passedFilename);
		return NULL;
	}
}

int checkSingleFile(const char *fpath, const struct stat *sb, int typeflag, struct FTW* ftwbuf, void* _arg){
	if (strstr(fpath,"HorribleSubs")!=NULL){
		return 0;
	}

	struct checkArg* _passedCheck = _arg;
	int _cachedRootStrlen = strlen(_passedCheck->rootChop);
	if (strncmp(fpath,_passedCheck->rootChop,_cachedRootStrlen)!=0){
		printf("Bad root to path. Path is: %s root is %s\n",fpath,_passedCheck->rootChop);
	}

	if (typeflag==FTW_F){
		char* _actualHash = hashFile(fpath);
		if (_actualHash!=NULL){
			signed char _fileMatched=-1;
			ITERATENLIST(_passedCheck->database,{
				struct singleDatabaseEntry* _currentEntry = _currentnList->data;
				if (strcmp(&(fpath[_cachedRootStrlen]),_currentEntry->path)==0){
					_fileMatched=(strcmp(_actualHash,_currentEntry->hash)==0);
					printf(REPORTMESSAGE,_fileMatched ? OKMESSAGE : BADMESSAGE, _actualHash, _currentEntry->hash, fpath);
					READYQUITITERATENLIST;
				}
			})
			if (_fileMatched==-1 || _fileMatched==0){ // File not in database or file wrong hash
				struct singleDatabaseEntry* _newEntry = malloc(sizeof(struct singleDatabaseEntry));
				_newEntry->hash = strdup(_actualHash);
				_newEntry->path = strdup(&(fpath[_cachedRootStrlen]));
				if (_fileMatched==0){ // File wrong hash
					addnList(&(_passedCheck->retBad))->data=_newEntry;
				}else{ // File not found
					printf(ADDNEWMESSAGE,fpath);
					addnList(&(_passedCheck->database))->data=_newEntry;
					_passedCheck->hasChangedDatabase=1;
				}
			}
		}else{
			printf("Failed to hash file %s\n",fpath);
		}
		free(_actualHash);
	}else if (typeflag!=FTW_D){
		printf("Unknown thing passed.\n%d:%s\n",typeflag,fpath);
		return 1;
	}
	return 0;
}
// Returns list of broken files
nList* checkDir(nList** _passedDatabase, char* _passedDirectory, char* _ret_DatabaseModified){
	if (_passedDirectory[strlen(_passedDirectory)-1]!=SEPARATOR){
		printf("NEED END WITH SEPARATOR, MORON");
		return NULL;
	}
	struct checkArg myCheckArgs;
	myCheckArgs.database = *_passedDatabase;
	myCheckArgs.retBad = NULL; 
	myCheckArgs.rootChop = _passedDirectory;
	nftwArg(_passedDirectory,checkSingleFile,5,FOLLOWSYMS ? 0 : FTW_PHYS, &myCheckArgs);
	*_ret_DatabaseModified = myCheckArgs.hasChangedDatabase;
	*_passedDatabase = myCheckArgs.database;
	return myCheckArgs.retBad;
}

int main(int argc, char** args){

	//printf("%s\n",hashFile("/home/nathan/Downloads/newanime/Slow Start/[Nii-sama] Slow Start - 01 [1080p][5F24D239].mkv"));
	//copyFile("/home/nathan/Downloads/newanime/Slow Start/[Nii-sama] Slow Start - 01 [1080p][5F24D239].mkv","/mnt/bla.mkv");
	//printf("%s\n",hashFile("/mnt/bla.mkv"));
	nList* _currentDatabase = readDatabase("./a");
	char _needResaveDatabase=0;
	nList* _brokenFiles = checkDir(&_currentDatabase,"/home/nathan/Downloads/newanime/",&_needResaveDatabase);
	if (_needResaveDatabase){
		writeDatabase(_currentDatabase,"./b");
	}else{
		printf("Database unchanged.");
	}
	

	//nftwArg("", getHashFromFilename, 5, FOLLOWSYMS ? 0 : FTW_PHYS,"bla");
/*
	nList* _list = readDatabase("./a");
	ITERATENLIST(_list,{
		printf("%s\n",((struct singleDatabaseEntry*)_currentnList->data)->path);
		printf("%s\n",((struct singleDatabaseEntry*)_currentnList->data)->hash);
	})
	freeDatabase(_list);
*/
}