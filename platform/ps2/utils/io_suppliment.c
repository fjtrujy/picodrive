#include <stdio.h>
#include <fileXio_rpc.h>
#include <unistd.h>
#include <string.h>

#include "io_suppliment.h"

int ps2_remove(const char *file){
	char cwd[FILENAME_MAX], path[FILENAME_MAX];
	int PathLength;

	getcwd(cwd, sizeof(cwd));
	PathLength=strlen(cwd);
	if(PathLength>=1 && (cwd[PathLength-1]=='/' || cwd[PathLength-1]=='\\')){
		cwd[--PathLength]='\0';
	}

	if(strchr(cwd, ':')!=NULL){
		sprintf(path, "%s/%s", cwd, file);
	}
	else{
		sprintf(path, "host:%s/%s", cwd, file);
	}
	return remove(path);
}

int ps2_mkdir(const char *path, int mode){
	char cwd[FILENAME_MAX], FullPath[FILENAME_MAX];
	int PathLength;

	getcwd(cwd, sizeof(cwd));
	PathLength=strlen(cwd);
	if(PathLength>=1 && (cwd[PathLength-1]=='/' || cwd[PathLength-1]=='\\')){
		cwd[--PathLength]='\0';
	}

	if(strchr(cwd, ':')!=NULL){
		sprintf(FullPath, "%s/%s", cwd, path);
	}
	else{
		sprintf(FullPath, "host:%s/%s", cwd, path);
	}
	return fileXioMkdir(FullPath, mode);
}
