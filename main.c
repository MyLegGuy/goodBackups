#define _XOPEN_SOURCE 500 // enable nftw
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
//
#include <zlib.h> // for crc32
//
#define HASHBUFFSIZE 16000
#define SEPARATOR '/'
#ifndef linux
	#warning forward slash is used for directory separator
#endif
#define FOLLOWSYMS 0
#define HASHLEN 8

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

const char* findCharBackwards(const char* _startHere, const char* _endHere, int _target){
	do{
		if (_startHere[0]==_target){
			return _startHere;
		}
		--_startHere;
	}while(_startHere>_endHere);
	return NULL;
}

char* tryGetHash(const char* _passedFilename, int _fakeStrlen){
	const char* _possibleHashStart = findCharBackwards(_passedFilename+_fakeStrlen-1,_passedFilename, '[');
	if (_possibleHashStart!=NULL && _possibleHashStart>strchr(_passedFilename, SEPARATOR)){
		++_possibleHashStart; // Skip past left bracket
		char* _possibleHashEnd = strchr(_possibleHashStart, ']');
		if (_possibleHashEnd!=NULL){
			int _hashLen = _possibleHashEnd-_possibleHashStart;
			if (_hashLen!=HASHLEN){
				--_possibleHashStart;
				return tryGetHash(_passedFilename,_possibleHashStart-_passedFilename);
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

int getHashFromFilename (const char *fpath, const struct stat *sb, int typeflag, struct FTW* ftwbuf){
	if (typeflag==FTW_F){
		if (strstr(fpath,"HorribleSubs")==NULL){
			char* _returned = tryGetHash(fpath,strlen(fpath));
			if (_returned!=NULL){
				printf("%s;%s\n",_returned,fpath);
				free(_returned);
			}else{
				printf("No hash for %s\n",fpath);
			}
		}
	}else if (typeflag!=FTW_D){
		printf("Unknown thing passed.\n%d:%s\n",typeflag,fpath);
		return 1;
	}
	return 0;
}

char* hashFile(char* _passedFilename){
	FILE* fp = fopen(_passedFilename,"rb");
	char* _lastBuf = malloc(HASHBUFFSIZE);
	uLong _finalHash = crc32(0L, Z_NULL, 0);
	while(!feof(fp)){
		size_t _numRead;
		_numRead = fread(_lastBuf,1,HASHBUFFSIZE,fp);
		_finalHash = crc32(_finalHash, _lastBuf, _numRead);
	}
    fclose(fp);
    char* _ret = malloc(HASHLEN);
	sprintf(_ret,"%08X",_finalHash);
	return _ret;
}

int main(int argc, char** args){
	//hashFile("/home/nathan/Downloads/newanime/Slow Start/[Nii-sama] Slow Start - 01 [1080p][5F24D239].mkv");
	nftw("/home/nathan/Downloads/newanime", getHashFromFilename, 5, FOLLOWSYMS ? 0 : FTW_PHYS);
}