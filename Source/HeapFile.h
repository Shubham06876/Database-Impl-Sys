
#ifndef HEAPFILE_H
#define HEAPFILE_H
#include "TwoWayList.h"
#include "Record.h"
#include "Schema.h"
#include "File.h"
#include "Comparison.h"
#include "ComparisonEngine.h"
#include "DBFile.h"



class HeapFile : public GenericDBFile{

	File file;
	char *filePath;
	Record *current;
	Page page;
	off_t poff;  // int value of number of completed pages
	off_t roff;  // reading page value
	bool pDirty; // true on page changes else false
	bool end;	// true on file end else false

public:
	HeapFile (); 
	~HeapFile();

	int Create(char *fpath, fType file_type, void *startup);
	int Open(char *fpath);
	int Close();

	void Load(Schema &myschema, char *loadpath);

	void MoveFirst();
	void Add(Record &addme);
	int GetNext(Record &fetchme);
	int GetNext(Record &fetchme, CNF &cnf, Record &literal);


};
#endif
