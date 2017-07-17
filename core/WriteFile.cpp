/*
 * WriteFile.cpp
 *
 *  Created on: 5 Oct 2015
 *      Author: giulio
 */

#include "WriteFile.h"
#include <glob.h>		// alternative to dirent.h to handle files in dirs
#include <stdlib.h>
//initialise static members
bool WriteFile::staticConstructed=false;
AuxiliaryTask WriteFile::writeAllFilesTask=NULL;
std::vector<WriteFile *> WriteFile::objAddrs(0);
bool WriteFile::threadRunning;
bool WriteFile::threadIsExiting;
int WriteFile::sleepTimeMs;

void WriteFile::staticConstructor(){
	if(staticConstructed==true)
		return;
	staticConstructed=true;
	threadIsExiting=false;
	threadRunning=false;
	writeAllFilesTask = Bela_createAuxiliaryTask(WriteFile::run, 60, "writeAllFilesTask", NULL);
}

WriteFile::WriteFile(){
	buffer = NULL;
	format = NULL;
	header = NULL;
	footer = NULL;
	stringBuffer = NULL;
	_filename = NULL;
};

char* WriteFile::generateUniqueFilename(const char* original)
{
	int originalLen = strlen(original);

	// search for a dot in the file (from the end)
	int dot = originalLen;
	for(int n = dot; n >= 0; --n)
	{
		if(original[n] == '.')
			dot = n;
	}
	char temp[originalLen + 2];
	int count = dot;
	snprintf(temp, count + 1, "%s", original);
	// add a * before the dot
	count += sprintf(temp + count, "*") - 1;
	count += sprintf(temp + count + 1, "%s", original + dot);

	// check how many log files are already there, and choose name according to this
	glob_t globbuf;
	glob(temp, 0, NULL, &globbuf);

	int logNum;
	int logMax = -1;
	// cycle through all and find the existing one with the highest index
	for(unsigned int i=0; i<globbuf.gl_pathc; i++)
	{
		logNum = atoi(globbuf.gl_pathv[i] + dot);
		if(logNum > logMax)
			logMax = logNum;
	}
	globfree(&globbuf);
	if(logMax == -1)
	{
		// use the same filename
		char* out = (char*)malloc(sizeof(char) * (originalLen +1));
		strcpy(out, original);
		return out;
	} else {
		// generate a new filename
		logNum = logMax + 1;	// new index
		count = snprintf(NULL, 0, "%d", logNum);
		char* out = (char*)malloc(sizeof(char) * (count + originalLen + 1));
		count = dot;
		snprintf(out, count + 1, "%s", original);
		count += sprintf(out + count, "%d", logNum) - 1;
		count += sprintf(out + count + 1, "%s", original + dot);
		printf("File %s exists, writing to %s instead\n", original, out);
		return out;
	}
}

void WriteFile::init(const char* filename, bool overwrite){ //if you do not call this before using the object, results are undefined
	if(!overwrite)
	{
		_filename = generateUniqueFilename(filename);
	} else {
		_filename = (char*)malloc(sizeof(char) * (strlen(filename) + 1));
		file = fopen(filename, "w");
	}
	file = fopen(_filename, "w");
	variableOpen = false;
	lineLength = 0;
	setEcho(false);
	bufferLength = 0;
	textReadPointer = 0;
	binaryReadPointer = 0;
	writePointer = 0;
	sleepTimeMs = 1;
	stringBufferLength = 1000;
	stringBuffer = (char*)malloc(sizeof(char) * (stringBufferLength));
	setHeader("variable=[\n");
	setFooter("];\n");
	staticConstructor(); //TODO: this line should be in the constructor, but cannot be because of a bug in Bela
	objAddrs.push_back(this);
	echoedLines = 0;
	echoPeriod = 1;
}

void WriteFile::setFileType(WriteFileType newFileType){
	fileType = newFileType;
	if(fileType == kBinary)
		setLineLength(1);
}
void WriteFile::setEcho(bool newEcho){
	echo=newEcho;
}
void WriteFile::setEchoInterval(int newEchoPeriod){
	echoPeriod = newEchoPeriod;
	if(echoPeriod != 0)
		echo = true;
	else
		echo = false;
}
void WriteFile::print(const char* string){
	if(echo == true){
		echoedLines++;
		if (echoedLines >= echoPeriod){
			echoedLines = 0;
			printf("%s", string);
		}
	}
	if(file != NULL && fileType != kBinary){
		fprintf(file, "%s", string);
	}
}

void WriteFile::writeLine(){
	if(echo == true || fileType != kBinary){
		int stringBufferPointer = 0;
		for(unsigned int n = 0; n < formatTokens.size(); n++){
			int numOfCharsWritten = snprintf( &stringBuffer[stringBufferPointer], stringBufferLength - stringBufferPointer,
								formatTokens[n], buffer[textReadPointer]);
			stringBufferPointer += numOfCharsWritten;
			textReadPointer++;
			if(textReadPointer >= bufferLength){
				textReadPointer -= bufferLength;
			}
		}
		print(stringBuffer);
	}
}

void WriteFile::setLineLength(int newLineLength){
	lineLength=newLineLength;
	free(buffer);
	bufferLength = lineLength * (int)1e5; // circular buffer
	buffer = (float*)malloc(sizeof(float) * bufferLength);
	if(buffer == NULL){
		fprintf(stderr, "Unable to allocate memory for the WriteFile buffer\n");
	}
}

void WriteFile::log(float value){
	if(fileType != kBinary && (format == NULL || buffer == NULL))
		return;
	buffer[writePointer] = value;
	writePointer++;
	if(writePointer == bufferLength){
		writePointer = 0;
	}
	if((fileType == kText && writePointer == textReadPointer - 1) ||
			(fileType == kBinary && writePointer == binaryReadPointer - 1)){
		rt_fprintf(stderr, "WriteFile: %s pointers crossed, you should probably slow down your writing to disk\n", _filename);
	}
	if(threadRunning == false){
		startThread();
	}
}

void WriteFile::log(const float* array, int length){
	for(int n = 0; n < length; n++){
		log(array[n]);
	}
}

WriteFile::~WriteFile() {
	free(format);
	free(buffer);
	free(header);
	free(footer);
	free(stringBuffer);
	free(_filename);
}

void WriteFile::setFormat(const char* newFormat){
	allocateAndCopyString(newFormat, &format);
	for(unsigned int n = 0; n < formatTokens.size(); n++){
		free(formatTokens[n]);
	}
	formatTokens.clear();
	int tokenStart = 0;
	bool firstToken = true;
	for(unsigned int n = 0; n < strlen(format)+1; n++){
		if(format[n] == '%' && format[n + 1] == '%'){
			n++;
		} else if (format[n] == '%' || format[n] == 0){
			if(firstToken == true){
				firstToken = false;
				continue;
			}
			char* string;
			unsigned int tokenLength = n - tokenStart;
			if(tokenLength == 0)
				continue;
			string = (char*)malloc((1+tokenLength)*sizeof(char));
			for(unsigned int i = 0; i < tokenLength; i++){
				string[i] = format[tokenStart + i];
			}
			string[tokenLength] = 0;
			formatTokens.push_back(string);
			tokenStart = n;
		}
	}
	setLineLength(formatTokens.size());
}

int WriteFile::getNumInstances(){
	return objAddrs.size();
}

void WriteFile::startThread(){
	Bela_scheduleAuxiliaryTask(writeAllFilesTask);
}

void WriteFile::stopThread(){
	threadIsExiting=true;
}

bool WriteFile::threadShouldExit(){
	return(gShouldStop || threadIsExiting);
}

bool WriteFile::isThreadRunning(){
	return threadRunning;
}

float WriteFile::getBufferStatus(){
	return 1-getOffset()/(float)bufferLength;
}

int WriteFile::getOffsetFromPointer(int aReadPointer){
	int offset = writePointer - aReadPointer;
		if( offset < 0)
			offset += bufferLength;
		return offset;
}
int WriteFile::getOffset(){
	if(fileType == kBinary){
		return getOffsetFromPointer(binaryReadPointer);
	}
	else{
		return getOffsetFromPointer(textReadPointer);
	}
}

void WriteFile::writeOutput(bool flush){
	while((echo == true || fileType == kText) && getOffsetFromPointer(textReadPointer) >= lineLength){ //if there is less than one line worth of data to write, skip over.
							 	 // So we make sure we only write full lines
		writeLine();
	}
	if(fileType == kBinary){
		int numBinaryElementsToWriteAtOnce = 4096;
		bool wasWritten = false;
		while(getOffsetFromPointer(binaryReadPointer) > numBinaryElementsToWriteAtOnce){
			int elementsToEndOfBuffer = bufferLength - binaryReadPointer;
			int numberElementsToWrite = numBinaryElementsToWriteAtOnce < elementsToEndOfBuffer ?
					numBinaryElementsToWriteAtOnce : elementsToEndOfBuffer;
			numberElementsToWrite = fwrite(&(buffer[binaryReadPointer]), sizeof(float), numberElementsToWrite, file);
			binaryReadPointer += numberElementsToWrite;
			if(binaryReadPointer >= bufferLength){
				binaryReadPointer = 0;
			}
			wasWritten = true;
		}
		if(flush == true){ // flush all the buffer to the file
			while(getOffsetFromPointer(binaryReadPointer) != 0){
				binaryReadPointer += fwrite(&(buffer[binaryReadPointer]), sizeof(float), 1, file);
				if(binaryReadPointer >= bufferLength){
					binaryReadPointer = 0;
				}
				wasWritten = true;
			}
		}
		if(wasWritten){
			fflush(file);
			fsync(fileno(file));
		}
	}
}

void WriteFile::writeAllOutputs(bool flush){
	for(unsigned int n = 0; n < objAddrs.size(); n++){
		objAddrs[n] -> writeOutput(flush);
	}
}

void WriteFile::writeAllHeaders(){
	for(unsigned int n = 0; n < objAddrs.size(); n++){
		objAddrs[n] -> writeHeader();
	}
}

void WriteFile::writeAllFooters(){
	for(unsigned int n = 0; n < objAddrs.size(); n++){
		objAddrs[n] -> writeFooter();
	}
}

void WriteFile::writeHeader(){
	print(header);
}

void WriteFile::writeFooter(){
	print(footer);
	fflush(file);
	fclose(file);
}

void WriteFile::setHeader(const char* newHeader){
	allocateAndCopyString(newHeader, &header);
	sanitizeString(header);
}

void WriteFile::setFooter(const char* newFooter){
	allocateAndCopyString(newFooter, &footer);
}

void WriteFile::sanitizeString(char* string){
	for(int unsigned n = 0; n < strlen(string); n++){ //purge %'s from the string
		if(string[n] == '%'){
			string[n] = ' ';
		}
	}
}

void WriteFile::run(void* arg){
	threadRunning = true;
	writeAllHeaders();
	while(threadShouldExit()==false){
		writeAllOutputs(false);
		usleep(sleepTimeMs*1000);
	}
	writeAllOutputs(true);
	writeAllFooters(); // when ctrl-c is pressed, the last line is closed and the file is closed
	threadRunning = false;
}

void WriteFile::allocateAndCopyString(const char* source, char** destination){
	free(*destination);
	*destination = (char*)malloc(sizeof(char) * (strlen(source) + 1));
	strcpy(*destination, source);
}
