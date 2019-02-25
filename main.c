#define _XOPEN_SOURCE 500 // enable nftw
#define _GNU_SOURCE // for getline
#include <stdio.h>
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
};
struct checkArg{
	char* rootChop;
	nList* database;
	nList* retBad;
	char hasChangedDatabase;
	long chosenActions;
};
//
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
		_currentEntry->seen=0;

		_currentEntry->path = malloc(_spaceSpot-_currentLine+1);
		memcpy(_currentEntry->path,_currentLine,_spaceSpot-_currentLine);
		_currentEntry->path[_spaceSpot-_currentLine]='\0';
		
		_currentEntry->hash = malloc(strlen(_spaceSpot));
		strcpy(_currentEntry->hash,_spaceSpot+1);

		addnList(&_ret)->data = _currentEntry;
		free(_currentLine);
	}
	fclose(fp);
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
void resetDatabaseSeen(nList* _passedList){
	ITERATENLIST(_passedList,{
		((struct singleDatabaseEntry*)_currentnList->data)->seen=0;
	})
}
char readABit(FILE* fp, char* _destBuffer, long* _numRead, long _maxRead){
	if (feof(fp)){
		return 1;
	}
	*_numRead = fread(_destBuffer,1,_maxRead,fp);
	return 0;
}

void lowCopyFile(const char* _srcPath, const char* _destPath, char _canMakeDirs){
	FILE* _destfp = fopen(_destPath,"wb");
	if (_destfp!=NULL){
		FILE* _sourcefp = fopen(_srcPath,"rb");
		if (_sourcefp!=NULL){
			char* _currentBit = malloc(COPYBUFF);
			size_t _lastRead;
			while (!readABit(_sourcefp,_currentBit,&_lastRead,COPYBUFF)){
				if (fwrite(_currentBit,1,_lastRead,_destfp)!=_lastRead){
					printf("wrote wrong number of bytes.\n");
				}
			}
			free(_currentBit);
			fclose(_sourcefp);
		}else{
			printf("Failed to open for reading %s\n",_srcPath);
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
						printf("Failed to make directory %s\n",_tempPath);
					}
				}
			}
			free(_tempPath);
		}

		if (_shouldRetry){
			lowCopyFile(_srcPath,_destPath,0);
		}else{
			printf("Failed to open for writing %s\n",_destPath);
		}
	}
}

void copyFile(const char* _srcPath, const char* _destPath){
	lowCopyFile(_srcPath,_destPath,1);
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
		printf("Failed to open %s\n",_passedFilename);
		return NULL;
	}
}

struct singleDatabaseEntry* getFromDatabase(nList* _passedDatabase, const char* _searchName){
	ITERATENLIST(_passedDatabase,{
		struct singleDatabaseEntry* _currentEntry = _currentnList->data;
		if (strcmp(_searchName,_currentEntry->path)==0){
			return _currentEntry;
		}
	})
	return NULL;
}

int checkSingleFile(const char *fpath, const struct stat *sb, int typeflag, struct FTW* ftwbuf, void* _arg){
	struct checkArg* _passedCheck = _arg;
	int _cachedRootStrlen = strlen(_passedCheck->rootChop);
	if (strncmp(fpath,_passedCheck->rootChop,_cachedRootStrlen)!=0){
		printf("Bad root to path. Path is: %s root is %s\n",fpath,_passedCheck->rootChop);
		return 1;
	}

	if (typeflag==FTW_F){
		char* _actualHash = NULL;
		if (_passedCheck->chosenActions & ACTION_CHECKEXISTING){
			_actualHash = hashFile(fpath);
		}
		if (_actualHash!=NULL || ((_passedCheck->chosenActions & ACTION_CHECKEXISTING)==0)){
			signed char _fileMatched=-1; // -1 means not in database, 0 means did not match, 1 means matched, 2 means do no more with this file
			struct singleDatabaseEntry* _matchingEntry = getFromDatabase(_passedCheck->database,&(fpath[_cachedRootStrlen]));

			if (_matchingEntry!=NULL){
				if (_matchingEntry->seen){
					printf("What? Already seen %s?\n",_matchingEntry->path);
				}
				_matchingEntry->seen=1;
				if (_passedCheck->chosenActions & ACTION_CHECKEXISTING){ // Hash must exist to get here
					_fileMatched=(strcmp(_actualHash,_matchingEntry->hash)==0);
					printf(REPORTMESSAGE,_fileMatched ? OKMESSAGE : BADMESSAGE, _actualHash, _matchingEntry->hash, fpath);
				}else{
					printf("Seen %s\n",fpath);
					_fileMatched=2;
				}
			}
			
			if (_fileMatched==-1 || _fileMatched==0){ // File not in database or file wrong hash.
				struct singleDatabaseEntry* _newEntry = malloc(sizeof(struct singleDatabaseEntry));
				_newEntry->path = strdup(&(fpath[_cachedRootStrlen]));
				_newEntry->seen = 1;
				_newEntry->hash = "Nothing should be here right now.";
				if (_fileMatched==0){ // File wrong hash. Only happens for ACTION_CHECKEXISTING
					_newEntry->hash = strdup(_actualHash);
					addnList(&(_passedCheck->retBad))->data=_newEntry;
				}else if (_fileMatched==-1 && (_passedCheck->chosenActions & ACTION_UPDATEDB)){ // File not found in database then add it

					if (_actualHash==NULL){
						_actualHash = hashFile(fpath);
					}
					if (_actualHash!=NULL){
						char* _possibleTagHash = getTagHash(fpath,strlen(fpath));
						if (_possibleTagHash!=NULL){ // Not in the database, but the filename has a hash we can use.
							//printf(ADDNEWWITHHASHMESSAGE,_newEntry->path);
							_fileMatched=(strcmp(_actualHash,_possibleTagHash)==0);
							printf(REPORTMESSAGE,_fileMatched ? OKMESSAGE : BADMESSAGE, _actualHash, _possibleTagHash, fpath);
							if (_fileMatched){
								// Can reuse our struct because our file matched so we don't need to add to bad list
								_newEntry->hash = _possibleTagHash;
								addnList(&(_passedCheck->database))->data=_newEntry;
							}else{
								// Need a new struct for the database representing the okay hash
								struct singleDatabaseEntry* _addThis = malloc(sizeof(struct singleDatabaseEntry));
								_addThis->path = strdup(_newEntry->path);
								_addThis->hash = _possibleTagHash;
								_addThis->seen = 1;
								addnList(&(_passedCheck->database))->data=_addThis;
								// and it's bad
								_newEntry->hash = strdup(_actualHash);
								addnList(&(_passedCheck->retBad))->data=_newEntry;
							}
						}else{ // No hash, not in database, assume our file is okay
							printf(ADDNEWMESSAGE,fpath);
							_newEntry->hash = strdup(_actualHash);
							addnList(&(_passedCheck->database))->data=_newEntry;
						}
						_passedCheck->hasChangedDatabase=1;
					}else{
						printf("(way2)Failed to hash file %s\n",fpath);
					}
				}else{
					free(_newEntry->path);
					free(_newEntry);
					printf("Do nothing with file %s not in database.\n",fpath);
				}
			}
		}else{
			printf("(way1)Failed to hash file %s\n",fpath);
		}
		free(_actualHash);
	}else if (typeflag!=FTW_D){
		printf("Unknown thing passed.\n%d:%s\n",typeflag,fpath);
		return 1;
	}
	return 0;
}
// Returns list of broken files
nList* checkDir(nList** _passedDatabase, char* _passedDirectory, char* _ret_DatabaseModified, long _passedActions){
	struct checkArg myCheckArgs;
	myCheckArgs.database = *_passedDatabase;
	myCheckArgs.retBad = NULL; 
	myCheckArgs.rootChop = _passedDirectory;
	myCheckArgs.hasChangedDatabase=0;
	myCheckArgs.chosenActions = _passedActions;
	nftwArg(_passedDirectory,checkSingleFile,5,FOLLOWSYMS ? 0 : FTW_PHYS, &myCheckArgs);
	*_ret_DatabaseModified = myCheckArgs.hasChangedDatabase;
	*_passedDatabase = myCheckArgs.database;
	return myCheckArgs.retBad;
}

char hasArg(char* _searchTarget, int argc, char** args){
	int i;
	for (i=0;i<argc;++i){
		if (strcmp(args[i],_searchTarget)==0){
			return 1;
		}
	}
	return 0;
}

int main(int argc, char** args){
	if (argc<3){
		printf("not enuff args.\n%s <db file> <primary folder> [backup folder 1] [backup folder ...] [optional args]\n",args[0]);
		return 0;
	}
	int i;
	int _numFolders=argc-2;
	long _passedActions = 0;
	char _addFromPrimaryOnly=1;
	if (access(args[1],F_OK)==-1){ // If our target database doesn't exist
		if (hasArg("--newdb",argc,args)){
			--_numFolders;
			printf("Making new database...\n");
			FILE* fp = fopen(args[1],"wb");
			if (fp!=NULL){
				fclose(fp);
			}else{
				printf("Failed to create newfile at %s\n",args[1]);
				return 1;
			}
			printf("Made new database.\n");
		}else{
			printf("%s does not exist. To make a new database, pass --newdb\n",args[1]);
			return 1;
		}
	}
	if (access(args[1],W_OK)==-1){
		printf("can't write to database file at %s\n",args[1]);
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
		_addFromPrimaryOnly=0;
	}
	//
	for (i=0;i<_numFolders;++i){
		if (args[i+2][strlen(args[i+2])-1]!=SEPARATOR){
			printf("All folder names need to end with %c. Failed on %s\n",SEPARATOR,args[i+2]);
			return 1;
		}
		if (args[i+2][0]=='.'){
			printf("All folder names should be absolute. Failed on %s\n",args[i+2]);
			return 1;
		}
		if (!dirExists(args[i+2])){
			printf("Directory does not exist %s\n",args[i+2]);
			return 1;
		}
	}
	if (_passedActions==0){
		printf("No actions specified.\n");
		return 1;
	}
	/////////////////
	nList* _currentDatabase = readDatabase(args[1]);
	nList* _brokenLists[_numFolders];

	for (i=0;i<_numFolders;++i){
		printf("Now checking folder %s\n",args[i+2]);
		char _needResaveDatabase=0;
		_brokenLists[i] = checkDir(&_currentDatabase,args[i+2],&_needResaveDatabase, _passedActions);
		if (_brokenLists[i]!=NULL){
			printf("=====\nBROKEN FILES\n======\n");
			ITERATENLIST(_brokenLists[i],{
				struct singleDatabaseEntry* _badEntry = _currentnList->data;
				struct singleDatabaseEntry* _expectedEntry = getFromDatabase(_currentDatabase,_badEntry->path);
				if (_expectedEntry!=NULL){
					printf(REPORTMESSAGE,BADMESSAGE, _badEntry->hash, _expectedEntry->hash, _badEntry->path);
				}else{
					printf("Well, it's broken, but couldn't find the following file in the database:\n\t%s:%s\n",_badEntry->path,_badEntry->hash);
				}
			})
		}else{
			printf("No broken files. :)\n");
		}
		if (_needResaveDatabase){
			writeDatabase(_currentDatabase,args[1]);
		}else{
			printf("No database changes.\n");
		}
		if ((_passedActions & ACTION_COPYMISSING) || (_passedActions & ACTION_LISTMISSING)){
			// Look for any files that should've been there but weren't
			ITERATENLIST(_currentDatabase,{
				struct singleDatabaseEntry* _currentEntry = _currentnList->data;
				if (!_currentEntry->seen){
					char* _destPath = malloc(strlen(_currentEntry->path)+strlen(args[i+2])+1);
					strcpy(_destPath,args[i+2]);
					strcat(_destPath,_currentEntry->path);
					if (_passedActions & ACTION_COPYMISSING){
						printf("Didn't see %s. Will try and put.\n",_destPath);
						char _worked=0;
						int j;
						for (j=0;j<_numFolders;++j){
							if (j==i){
								continue;
							}
							char* _tempPath = malloc(strlen(_currentEntry->path)+strlen(args[j+2])+1);
							strcpy(_tempPath,args[j+2]);
							strcat(_tempPath,_currentEntry->path);
							if (fileExists(_tempPath)){
								char* _possibleHash = hashFile(_tempPath);
								if (_possibleHash!=NULL){
									if (strcmp(_possibleHash,_currentEntry->hash)==0){
										printf("Copying and hashing %s to %s\n",_tempPath,_destPath);
										copyFile(_tempPath,_destPath);
										// Check dest file after copied
										free(_possibleHash);
										_possibleHash = hashFile(_destPath); // Now this refers to the hash of our dest file
										if (_possibleHash!=NULL){
											if (strcmp(_currentEntry->hash,_possibleHash)==0){
												printf("Successful copy.\n");
												_worked=1;
											}else{
												printf("Wrong hash after copying to %s. Expected %s.\n",_destPath,_currentEntry->hash);
											}
											free(_possibleHash);
										}else{
											printf("Failed to hash dest file after copying %s to %s\n",_tempPath,_destPath);
										}
									}else{
										printf("Copy candidate at %s hash does not line up. Got %s, expected %s.\n",_tempPath,_possibleHash,_currentEntry->hash);
										free(_possibleHash);
									}
								}else{
									printf("Failed to hash existing copy source candidate at %s\n",_tempPath);
								}
							}
							free(_tempPath);
						}
						if (!_worked){
							printf("Failed to find a copy of %s to copy to %s.\n",_currentEntry->path,_destPath);
						}
					}else{
						printf("Didn't see %s.\n",_destPath);
					}
					free(_destPath);
				}
			})
		}

		resetDatabaseSeen(_currentDatabase);

		if (i==0 && _addFromPrimaryOnly){
			_passedActions &= ~(1UL << 2);
		}
	}
	for (i=0;i<_numFolders;++i){
		freeDatabase(_brokenLists[i]);
	}
	freeDatabase(_currentDatabase);
}